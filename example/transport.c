#include "transport.h"
#include "pcp/ip.h"
#include "pcp/pcp.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

typedef struct size_or_error {
  bool has_value;
  union {
    size_t value;
    errno_t error;
  };
} size_or_error_t;

static size_or_error_t transport_send(size_t size, uint8_t const data[size]);
static size_or_error_t transport_recv(size_t size, uint8_t data[size]);

static pcp_time_ms_t transport_now() {
  struct timespec ts = {0};
  [[maybe_unused]] int const result = clock_gettime(CLOCK_MONOTONIC, &ts);
  assert(0 == result);
  return (pcp_time_ms_t)ts.tv_sec * 1000 + (pcp_time_ms_t)ts.tv_nsec / 1000000;
}

static int client = -1;
static volatile sig_atomic_t running = 1;

static void on_signal(int /*signal*/) { running = 0; }

static struct sockaddr_in6 to_sockaddr_in6(socket_address_t const *address) {
  // ip_address_t stores IPv4 as v4-mapped, which is what a dual-stack socket
  // wants
  struct sockaddr_in6 result = {0};
  result.sin6_family = AF_INET6;
  result.sin6_port = htons(address->port);
  static_assert(sizeof(result.sin6_addr.s6_addr) == sizeof(address->ip.addr));
  memcpy(result.sin6_addr.s6_addr, address->ip.addr,
         sizeof(result.sin6_addr.s6_addr));
  return result;
}

socket_address_t transport_init(socket_address_t server) {
  client = socket(AF_INET6, SOCK_DGRAM, 0);
  if (-1 == client) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // accept IPv4 servers as v4-mapped addresses as well
  int const v6only = 0;
  if (-1 ==
      setsockopt(client, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only))) {
    perror("setsockopt(IPV6_V6ONLY)");
    exit(EXIT_FAILURE);
  }

  // connect() picks our source address (needed for the PCP client address
  // field) and filters out datagrams from anyone else
  struct sockaddr_in6 addr = to_sockaddr_in6(&server);
  if (-1 == connect(client, (struct sockaddr *)&addr, sizeof(addr))) {
    perror("connect");
    exit(EXIT_FAILURE);
  }

  int const flags = fcntl(client, F_GETFL);
  if (-1 == flags || -1 == fcntl(client, F_SETFL, flags | O_NONBLOCK)) {
    perror("fcntl");
    exit(EXIT_FAILURE);
  }

  struct sigaction action = {0};
  action.sa_handler = on_signal;
  sigemptyset(&action.sa_mask);
  // no SA_RESTART so select() returns with EINTR and the loop can exit
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);

  socklen_t size = sizeof(addr);
  [[maybe_unused]] const int result =
      getsockname(client, (struct sockaddr *)&addr, &size);
  assert(0 == result);

  return socket_address_from_sockaddr_in6(&addr);
}

void transport_close() {
  if (-1 != client) {
    close(client);
    client = -1;
  }
}

void transport_loop(pcp_session_t *session, bool until_idle) {
  // outgoing packets use the session's buffer (see main)
  uint8_t incoming[PCP_PACKET_MAX_SIZE];

  // every loop needs its own signal to stop
  running = 1;
  while (running) {
    // poll until the session tells us how long to wait
    pcp_optional_time_ms_t deadline = {0};
    for (bool polling = true; polling;) {
      auto const event = pcp_session_poll(session, transport_now());
      switch (event.type) {
      case PCP_EVENT_SEND: {
        auto const sent = transport_send(event.send.size, event.send.data);
        if (!sent.has_value) {
          // EAGAIN too, the retransmission will cover it
          fprintf(stderr, "send: %s\n", strerror(sent.error));
        }
        break;
      }
      case PCP_EVENT_MAPPED: {
        char external[SOCKET_ADDRESS_STRING_LENGTH];
        socket_address_ntop(sizeof(external), external, &event.mapped.external);
        printf("Mapping %" PRIu64 " is reachable at %s\n", event.mapped.handle,
               external);
        break;
      }
      case PCP_EVENT_UNMAPPED:
        printf("Mapping %" PRIu64 " lost its external address\n",
               event.unmapped.handle);
        break;
      case PCP_EVENT_FAILED:
        printf("Mapping %" PRIu64 " failed with result %d\n",
               event.failed.handle, event.failed.result);
        break;
      case PCP_EVENT_TIMEOUT:
        deadline = event.timeout.deadline;
        polling = false;
        break;
      }
    }

    if (until_idle && !deadline.has_value) {
      return;
    }

    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(client, &readable);

    pcp_time_ms_t const current = transport_now();
    pcp_time_ms_t const remaining =
        deadline.value > current ? deadline.value - current : 0;
    struct timeval timeout = {
        .tv_sec = (time_t)(remaining / 1000),
        .tv_usec = (suseconds_t)(remaining % 1000 * 1000),
    };
    // without a deadline we block until a datagram or signal arrives
    switch (select(client + 1, &readable, nullptr, nullptr,
                   deadline.has_value ? &timeout : nullptr)) {
    case -1:
      if (errno == EINTR) { // we'll only retry on interrupt
        continue;
      }
      perror("select");
      exit(EXIT_FAILURE);
      break;
    case 0: // the deadline passed, polling at the top of the loop handles it
      break;
    default:
      if (FD_ISSET(client, &readable)) {
        while (true) {
          auto const size = transport_recv(sizeof(incoming), incoming);
          if (!size.has_value) {
            if (size.error != EAGAIN && size.error != EWOULDBLOCK) {
              // e.g. ECONNREFUSED if nothing is listening, the
              // retransmissions keep trying
              fprintf(stderr, "recv: %s\n", strerror(size.error));
            }
            break;
          }
          pcp_session_on_datagram(session, transport_now(), size.value,
                                  incoming);
        }
      }
      break;
    }
  }
}

static size_or_error_t transport_send(size_t size, uint8_t const data[size]) {
  ssize_t const sent = send(client, data, size, 0);
  if (sent < 0) {
    return (size_or_error_t){.has_value = false, .error = errno};
  }

  return (size_or_error_t){.has_value = true, .value = (size_t)sent};
}

static size_or_error_t transport_recv(size_t size, uint8_t data[size]) {
  ssize_t const read = recv(client, data, size, 0);
  if (read < 0) {
    return (size_or_error_t){.has_value = false, .error = errno};
  }

  return (size_or_error_t){.has_value = true, .value = (size_t)read};
}
