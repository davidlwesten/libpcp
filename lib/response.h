#pragma once

#include <pcp/ip.h>
#include <pcp/protocol.h>
#include <pcp/result.h>

#include "types.h"

#include <time.h>

typedef struct pcp_response_header {
  pcp_opcode_t opcode;
  pcp_result_t code;
  time_t ttl;
  time_t epoch;
} pcp_response_header_t;

typedef struct pcp_peer_response {
  pcp_response_header_t header;
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  port_t internal;
  socket_address_t external;
  socket_address_t peer;
} pcp_peer_response_t;

typedef struct pcp_map_response {
  pcp_response_header_t header;
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  port_t internal;
  socket_address_t external;
} pcp_map_response_t;

typedef struct pcp_announce_response {
  pcp_response_header_t header;
} pcp_announce_response_t;

typedef union pcp_response {
  pcp_response_header_t header;
  pcp_map_response_t map;
  pcp_peer_response_t peer;
  pcp_announce_response_t announce;
} pcp_response_t;
