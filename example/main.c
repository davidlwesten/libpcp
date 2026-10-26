#include <pcp/pcp.h>

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#include "pcp/ip.h"
#include "transport.h"

static bool pcp_rng(void * /*ctx*/, size_t count, uint8_t bytes[count]);

const char *address_to_string(socket_address_t const *addr) {
  static char buffer[SOCKET_ADDRESS_STRING_LENGTH];
  socket_address_to_string(sizeof(buffer), buffer, addr);
  return buffer;
}

int main(int argc, char *argv[]) {
  socket_address_t server = {
      .ip = ip_address_loopback_v4,
      .port = 5351,
  };

  if (argc > 1) {
    auto const parsed_server = socket_address_from_string(argv[1]);
    if (!parsed_server.has_value) {
      fprintf(stderr, "usage: %s <server-address>:<port>\n", argv[0]);
      fprintf(stderr, "e.g.   %s 192.168.1.1:5351\n", argv[0]);
      return EXIT_FAILURE;
    }
    server = parsed_server.value;
  }

  socket_address_t const local = transport_init(server);
  printf("Client on %s\n", address_to_string(&local));

  uint8_t buffer[PCP_PACKET_MAX_SIZE];
  pcp_config_t config = {.local = local.ip,
                         .allocator = {0},
                         .rng =
                             {
                                 .ctx = nullptr,
                                 .fill = pcp_rng,
                             },
                         .buffer = {
                             .size = sizeof(buffer),
                             .data = buffer,
                         }};

  pcp_session_t *session = pcp_session_begin(config);

  auto const handle =
      pcp_session_map_request(session, 1024, UDP, 12345, 0, nullptr);
  if (!handle.has_value) {
    fprintf(stderr, "pcp_session_map_request: %s\n", strerror(handle.error));
    return EXIT_FAILURE;
  }

  transport_loop(session, false);

  // delete the mapping instead of leaving it to expire. A second Ctrl-C skips
  // waiting for that.
  printf("Releasing mapping %" PRIu64 ", interrupt again to quit right away\n",
         handle.value);
  pcp_session_release(session, handle.value);
  transport_loop(session, true);

  pcp_session_end(session);
  transport_close();
  return EXIT_SUCCESS;
}

static bool pcp_rng(void * /*ctx*/, size_t count, uint8_t bytes[count]) {
  size_t filled = 0;
  while (filled < count) {
    ssize_t const result = getrandom(bytes + filled, count - filled, 0);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    filled += (size_t)result;
  }
  return true;
}
