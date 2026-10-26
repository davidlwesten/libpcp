#pragma once

#include <pcp/pcp.h>

#include "nonce.h"

// A parsed response. ANNOUNCE responses are just the header.
typedef struct pcp_response {
  pcp_opcode_t type;
  pcp_result_t result;
  pcp_time_s_t lifetime; // granted lifetime, or how long an error applies
  pcp_time_s_t epoch;    // the server's epoch, for detecting restarts
  union {
    struct {
      pcp_nonce_t nonce;
      pcp_protocol_t protocol;
      port_t port;               // internal port
      socket_address_t external; // assigned
    } map;
    struct {
      pcp_nonce_t nonce;
      pcp_protocol_t protocol;
      port_t port;               // internal port
      socket_address_t external; // assigned
      socket_address_t peer;
    } peer;
  };
} pcp_response_t;
