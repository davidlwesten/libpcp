#pragma once

#include "datagram.h"
#include "socket.h"

typedef struct transport {
  socket_address_t local;
  socket_address_t remote;
  void (*send)(struct transport const *self,
               size_t size,
               const uint8_t src[size],
               void (*callback)(size_or_error_t result));
  void (*run)(struct transport const *self);
  void (*close)(struct transport const *self);
  void *ctx;
} transport_t;

typedef void (*on_transport_data_callback_t)(transport_t const *transport,
                                             void *ctx,
                                             const_view_t datagram);
typedef struct on_transport_data {
  on_transport_data_callback_t callback;
  void *ctx;
} on_transport_data_t;

typedef struct transport_or_error {
  bool has_value;
  union {
    transport_t value;
    errno_t error;
  };
} transport_or_error_t;

transport_or_error_t transport_connect(const char *server,
                                       on_transport_data_t callback);
