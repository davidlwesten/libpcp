#pragma once

#include <pcp/pcp.h>

#include "nonce.h"

// A response as received from the server, see RFC 6887 §7.2, §11.1 and §12.1
typedef struct pcp_response {
  pcp_opcode_t type;
  pcp_nonce_t nonce;
  pcp_result_t result;
  pcp_time_s_t lifetime; // granted lifetime, or how long an error applies
  pcp_time_s_t epoch;    // the server's epoch, for detecting restarts
  pcp_protocol_t protocol;
  port_t port; // internal port
  union {
    struct {
      socket_address_t external; // assigned
    } map;
    struct {
      socket_address_t external; // assigned
      socket_address_t peer;
    } peer;
  };
} pcp_response_t;
