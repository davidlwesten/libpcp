#pragma once

#include "datagram.h"
#include "socket.h"

struct transport;

typedef void (*transport_on_send_callback_t)(struct transport const *self,
                                             void *ctx,
                                             size_or_error_t result);

typedef struct transport_on_send {
  transport_on_send_callback_t fn;
  void *ctx;
} transport_on_send_t;

typedef struct transport {
  socket_address_t local;
  socket_address_t remote;
  void *ctx;
  void (*send)(struct transport const *self,
               const_view_t data,
               transport_on_send_t callback);
  void (*run)(struct transport const *self);
  void (*stop)(struct transport const *self);
  void (*close)(struct transport const *self);
} transport_t;

typedef void (*transport_on_data_callback_t)(transport_t const *transport,
                                             void *ctx,
                                             const_view_t datagram);

typedef struct transport_on_data {
  transport_on_data_callback_t fn;
  void *ctx;
} transport_on_data_t;

typedef struct transport_or_error {
  bool has_value;
  union {
    transport_t value;
    errno_t error;
  };
} transport_or_error_t;

transport_or_error_t transport_connect(const char *server,
                                       transport_on_data_t callback);
