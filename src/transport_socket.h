#pragma once

#include <pcp/transport.h>

#include "socket.h"

#include <stddef.h>

constexpr size_t TRANSPORT_SOCKET_MAX_TIMERS = 16;

typedef struct transport_socket_timer {
  bool active;
  uint32_t id;
  double expiration;
  pcp_transport_on_timer_t action;
} transport_socket_timer_t;

typedef struct transport_socket {
  socket_t socket;
  transport_socket_timer_t timers[TRANSPORT_SOCKET_MAX_TIMERS];
} transport_socket_t;

typedef struct transport_socket_result {
  bool has_value;
  union {
    transport_socket_t value;
    errno_t error;
  };
} transport_socket_result_t;

transport_socket_result_t transport_socket_connect(const char *server);
pcp_transport_t transport_socket_interface(transport_socket_t *transport);
void transport_socket_run(transport_socket_t *transport, void *context);
