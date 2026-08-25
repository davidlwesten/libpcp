#pragma once

#include "socket.h"

typedef struct transport transport_t;

typedef void (*transport_on_send_callback_t)(void *transportdata,
                                             void *userdata,
                                             size_or_error_t result);

typedef struct transport_on_send {
  transport_on_send_callback_t fn;
  void *userdata;
} transport_on_send_t;

typedef void (*transport_on_timer_callback_t)(void *userdata);

typedef struct transport_on_timer {
  transport_on_timer_callback_t fn;
  void *userdata;
} transport_on_timer_t;

typedef struct transport_timer {
  void (*stop)(struct transport_timer const *self);
} transport_timer_t;

typedef void (*transport_run_t)(transport_t const *self);
typedef void (*transport_stop_t)(transport_t const *self);
typedef void (*transport_close_t)(transport_t const *self);
typedef void (*transport_send_t)(transport_t const *self,
                                 const_view_t data,
                                 transport_on_send_t callback);
typedef transport_timer_t (*transport_add_timer_t)(transport_t *self,
                                                   time_t delay,
                                                   transport_on_timer_t action);

typedef struct transport {
  socket_address_t local;
  socket_address_t remote;
  transport_send_t send;
  transport_add_timer_t add_timer;
  transport_run_t run;
  transport_stop_t stop;
  transport_close_t close;
} transport_t;

typedef void (*transport_on_recv_callback_t)(void *transportdata,
                                             void *userdata,
                                             const_view_t packet);

typedef struct transport_on_recv {
  transport_on_recv_callback_t fn;
  void *userdata;
} transport_on_recv_t;

typedef struct transport_or_error {
  bool has_value;
  union {
    transport_t *value;
    errno_t error;
  };
} transport_or_error_t;

transport_or_error_t transport_connect(const char *server,
                                       transport_on_recv_t callback,
                                       void *userdata);
