#pragma once

#include <time.h>

#include "ip.h"
#include "option.h"
#include "protocol.h"

#define CALL(POINTER, MEMBER, ...)                                             \
  ((POINTER)->MEMBER((POINTER)__VA_OPT__(, ) __VA_ARGS__))

typedef struct pcp_client {
  void (*map)(struct pcp_client *self,
              pcp_protocol_t protocol,
              port_t internal,
              socket_address_t external,
              time_t ttl,
              size_t option_count,
              const pcp_option_t[option_count]);
  void (*peer)(struct pcp_client *self,
               pcp_protocol_t protocol,
               port_t internal,
               socket_address_t external,
               socket_address_t peer,
               time_t ttl,
               size_t option_count,
               const pcp_option_t[option_count]);
  void (*run)(struct pcp_client *self);
} pcp_client_t;

pcp_client_t *pcp_client_create(const char *server);
void pcp_client_destroy(pcp_client_t *client);
