#include "socket.h"

#include "transport_socket.h"

#include <pcp/pcp.h>
#include <pcp/transport.h>

#include <alloca.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

#include <sys/select.h>

#define CALL_SOCKET(POINTER, MEMBER, ...)                                      \
  ((POINTER)->MEMBER((POINTER)__VA_OPT__(, ) __VA_ARGS__))

static void send_(transport_socket_t *self,
                  pcp_transport_const_view_t src,
                  pcp_transport_on_send_t closure) {
  auto const result = CALL_SOCKET(&self->socket, send, src);
  if (closure.fn) {
    closure.fn(closure.userdata, result);
  }
}

static socket_address_t transport_local(transport_socket_t *self) {
  return self->socket.local;
}

static socket_address_t transport_remote(transport_socket_t *self) {
  return self->socket.remote;
}

static void transport_remove_timer(transport_socket_t *transport,
                                   pcp_transport_timer_t id) {
  assert(transport);

  for (size_t i = 0; i < TRANSPORT_SOCKET_MAX_TIMERS; ++i) {
    auto const timer = &transport->timers[i];
    if (timer->active && timer->id == id) {
      timer->active = false;
      return;
    }
  }
}

static pcp_transport_timer_result_t
transport_add_timer(void *data,
                    pcp_transport_timeout_t ttl,
                    pcp_transport_on_timer_t action) {
  transport_socket_t *self = data;
  assert(self);
  assert(ttl.seconds > 0);

  for (size_t i = 0; i < TRANSPORT_SOCKET_MAX_TIMERS; ++i) {
    auto const timer = &self->timers[i];
    if (!timer->active) {
      static uint32_t next_id = 0;
      *timer = (transport_socket_timer_t){.active = true,
                                          .id = next_id++,
                                          .expiration = (double)time(nullptr) +
                                                        ttl.seconds,
                                          .action = action};
      return (pcp_transport_timer_result_t){.has_value = true,
                                            .value = timer->id};
    }
  }

  return (pcp_transport_timer_result_t){.has_value = false, .error = ENOSPC};
}

void transport_socket_run(transport_socket_t *self, void *context) {
  uint8_t packet_data[1100] = {0};

  for (;;) {
    fd_set readfds = {0};
    FD_SET(self->socket.fd, &readfds);

    double now = (double)time(nullptr);

    struct timeval timeval = {0};
    struct timeval *timeout = nullptr;
    auto next = self->timers + TRANSPORT_SOCKET_MAX_TIMERS;
    for (size_t i = 0; i < TRANSPORT_SOCKET_MAX_TIMERS; ++i) {
      auto const timer = &self->timers[i];
      if (timer->active &&
          (next == self->timers + TRANSPORT_SOCKET_MAX_TIMERS ||
           timer->expiration < next->expiration)) {
        next = timer;
      }
    }
    if (next != self->timers + TRANSPORT_SOCKET_MAX_TIMERS) {
      double const delta = fmax(0.0, next->expiration - now);
      timeval.tv_sec = (time_t)delta;
      timeval.tv_usec =
          (suseconds_t)((delta - (double)timeval.tv_sec) * 1000000.0);
      timeout = &timeval;
    }

    int const result =
        select(self->socket.fd + 1, &readfds, nullptr, nullptr, timeout);

    switch (result) {
    case -1:
      assert(errno == EINTR);
    case 0:
      now = (double)time(nullptr);

      for (;;) {
        auto next_due = self->timers + TRANSPORT_SOCKET_MAX_TIMERS;
        for (size_t i = 0; i < TRANSPORT_SOCKET_MAX_TIMERS; ++i) {
          auto const timer = &self->timers[i];
          if (timer->active &&
              (next_due == self->timers + TRANSPORT_SOCKET_MAX_TIMERS ||
               timer->expiration < next_due->expiration)) {
            next_due = timer;
          }
        }
        if (next_due == self->timers + TRANSPORT_SOCKET_MAX_TIMERS ||
            !isless(next_due->expiration, now)) {
          break;
        }
        auto const action = next_due->action;
        next_due->active = false;
        action.fn(action.userdata);
      }
      break;
    case 1: {
      auto const size =
          CALL_SOCKET(&self->socket, recv,
                      (pcp_transport_view_t){.size = sizeof(packet_data),
                                             .data = packet_data});
      if (!size.has_value) {
        break;
      }

      if (size.value == 0) {
        return;
      }

      pcp_client_recv(context,
                      (pcp_transport_const_view_t){.size = size.value,
                                                   .data = packet_data});
    } break;
    }
  }
}

static void transport_stop(transport_socket_t *self) {
  CALL_SOCKET(&self->socket, shutdown);
}

static void transport_close(transport_socket_t *self) {
  CALL_SOCKET(&self->socket, close);
}

transport_socket_result_t transport_socket_connect(const char *server) {
  auto socket = socket_connect(server, "pcp");
  if (!socket.has_value) {
    return (transport_socket_result_t){.has_value = false,
                                       .error = socket.error};
  }

  return (transport_socket_result_t){
      .has_value = true,
      .value = {.socket = socket.value, .timers = {}}};
}

pcp_transport_t transport_socket_interface(transport_socket_t *transport) {
  assert(transport);
  return (pcp_transport_t){
      .data = transport,
      .local = (pcp_transport_endpoint_t)transport_local,
      .remote = (pcp_transport_endpoint_t)transport_remote,
      .send = (pcp_transport_send_t)send_,
      .add_timer = (pcp_transport_add_timer_t)transport_add_timer,
      .remove_timer = (pcp_transport_remove_timer_t)transport_remove_timer,
      .stop = (pcp_transport_stop_t)transport_stop,
      .close = (pcp_transport_close_t)transport_close};
}
