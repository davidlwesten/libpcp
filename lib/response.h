#pragma once

#include <pcp/pcp.h>

#include "nonce.h"

/**
 * @brief A parsed response. ANNOUNCE responses are just the header.
 */
typedef struct pcp_response {
  pcp_opcode_t type;
  pcp_result_t result;
  /// Granted lifetime, or how long an error applies
  pcp_time_s_t lifetime;
  /// The server's epoch, for detecting restarts
  pcp_time_s_t epoch;
  union {
    struct {
      pcp_nonce_t nonce;
      pcp_protocol_t protocol;
      /// Internal port
      port_t port;
      /// Assigned external address
      socket_address_t external;
    } map;
    struct {
      pcp_nonce_t nonce;
      pcp_protocol_t protocol;
      /// Internal port
      port_t port;
      /// Assigned external address
      socket_address_t external;
      socket_address_t peer;
    } peer;
  };
} pcp_response_t;
