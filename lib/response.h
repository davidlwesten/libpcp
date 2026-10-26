#pragma once

#include <pcp/pcp.h>

#include "nonce.h"

typedef struct pcp_response {
  pcp_opcode_t type;
  pcp_result_t result;
  pcp_time_s_t lifetime;
  pcp_time_s_t epoch;
  union {
    struct {
      pcp_nonce_t nonce;
      pcp_protocol_t protocol;
      port_t port;
      socket_address_t external;
    } map;
    struct {
      pcp_nonce_t nonce;
      pcp_protocol_t protocol;
      port_t port;
      socket_address_t external;
      socket_address_t peer;
    } peer;
  };
} pcp_response_t;
