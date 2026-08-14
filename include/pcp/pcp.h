#pragma once

#include <errno.h>
#include <time.h>

#include "ip.h"
#include "option.h"
#include "protocol.h"
#include "result.h"

#define CALL(POINTER, MEMBER, ...)                                             \
  ((POINTER)->MEMBER((POINTER)__VA_OPT__(, ) __VA_ARGS__))

typedef struct pcp_mapping {
  pcp_protocol_t protocol;
  socket_address_t internal;
  socket_address_t external;
  time_t ttl;
} pcp_mapping_t;

typedef typeof(errno) errno_t;

typedef struct pcp_mapping_or_error {
  pcp_result_t code;
  union {
    pcp_mapping_t value;
    errno_t error;
  };
} pcp_mapping_or_error_t;

typedef struct pcp_client {
  socket_address_t local;
  socket_address_t remote;
  pcp_mapping_or_error_t (*map)(const struct pcp_client *,
                                pcp_protocol_t protocol,
                                port_t internal,
                                socket_address_t external,
                                time_t ttl,
                                size_t option_count,
                                const pcp_option_t[option_count]);
  pcp_mapping_or_error_t (*peer)(const struct pcp_client *,
                                 pcp_protocol_t protocol,
                                 port_t internal,
                                 socket_address_t external,
                                 socket_address_t peer,
                                 time_t ttl,
                                 size_t option_count,
                                 const pcp_option_t[option_count]);
} pcp_client_t;

pcp_client_t *pcp_client_create(const char *server);
void pcp_client_destroy(pcp_client_t *client);
