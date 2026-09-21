#pragma once

#include <time.h>

#include "ip.h"
#include "option.h"
#include "protocol.h"
#include "transport.h"

#define CALL(POINTER, MEMBER, ...)                                             \
  ((POINTER)->MEMBER((POINTER)->data __VA_OPT__(, ) __VA_ARGS__))

typedef struct pcp_client {
  void *data;
  void (*map)(void *data,
              pcp_protocol_t protocol,
              port_t internal,
              socket_address_t external,
              time_t ttl,
              size_t option_count,
              const pcp_option_t[option_count]);
  void (*peer)(void *data,
               pcp_protocol_t protocol,
               port_t internal,
               socket_address_t external,
               socket_address_t peer,
               time_t ttl,
               size_t option_count,
               const pcp_option_t[option_count]);
} pcp_client_t;

typedef struct pcp_client_result {
  bool has_value;
  union {
    pcp_client_t value;
    errno_t error;
  };
} pcp_client_result_t;

pcp_client_result_t pcp_client_create(pcp_transport_t transport);
void pcp_client_destroy(pcp_client_t *client);
void pcp_client_recv(void *data, pcp_transport_const_view_t packet);
