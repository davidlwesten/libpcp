#pragma once

#include <pcp/ip.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <errno.h>

typedef struct pcp_transport pcp_transport_t;

typedef typeof(errno) errno_t;

typedef struct pcp_transport_const_view {
  size_t size;
  uint8_t const *data;
} pcp_transport_const_view_t;

typedef struct pcp_transport_view {
  size_t size;
  uint8_t *data;
} pcp_transport_view_t;

typedef struct pcp_transport_size_or_error {
  bool has_value;
  union {
    size_t value;
    errno_t error;
  };
} pcp_transport_size_or_error_t;

typedef void (*pcp_transport_on_send_callback_t)(
    void *userdata,
    pcp_transport_size_or_error_t result);

typedef struct pcp_transport_on_send {
  pcp_transport_on_send_callback_t fn;
  void *userdata;
} pcp_transport_on_send_t;

typedef void (*pcp_transport_on_timer_callback_t)(void *userdata);

typedef struct pcp_transport_on_timer {
  pcp_transport_on_timer_callback_t fn;
  void *userdata;
} pcp_transport_on_timer_t;

typedef uint32_t pcp_transport_timer_t;

typedef struct pcp_transport_timer_result {
  bool has_value;
  union {
    pcp_transport_timer_t value;
    errno_t error;
  };
} pcp_transport_timer_result_t;

typedef struct pcp_transport_timeout {
  double seconds;
} pcp_transport_timeout_t;

typedef socket_address_t (*pcp_transport_endpoint_t)(void *data);
typedef void (*pcp_transport_run_t)(void *data, void *context);
typedef void (*pcp_transport_stop_t)(void *data);
typedef void (*pcp_transport_close_t)(void *data);
typedef void (*pcp_transport_send_t)(void *data,
                                     pcp_transport_const_view_t packet,
                                     pcp_transport_on_send_t callback);
typedef pcp_transport_timer_result_t (*pcp_transport_add_timer_t)(
    void *data,
    pcp_transport_timeout_t delay,
    pcp_transport_on_timer_t action);
typedef void (*pcp_transport_remove_timer_t)(void *data,
                                             pcp_transport_timer_t timer);

struct pcp_transport {
  void *data;
  pcp_transport_endpoint_t local;
  pcp_transport_endpoint_t remote;
  pcp_transport_send_t send;
  pcp_transport_add_timer_t add_timer;
  pcp_transport_remove_timer_t remove_timer;
  pcp_transport_run_t run;
  pcp_transport_stop_t stop;
  pcp_transport_close_t close;
};
