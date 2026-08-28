#pragma once

#include <pcp/ip.h>
#include <pcp/option.h>
#include <pcp/protocol.h>

#include "types.h"

#include <time.h>

typedef struct pcp_request_header {
  pcp_opcode_t opcode;
  time_t ttl;
  socket_address_t internal;
} pcp_request_header_t;

typedef struct pcp_map_request {
  pcp_request_header_t header;
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  socket_address_t external;
} pcp_map_request_t;

typedef struct pcp_peer_request {
  pcp_request_header_t header;
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  socket_address_t external;
  socket_address_t peer;
} pcp_peer_request_t;

typedef struct pcp_announce_request {
  pcp_request_header_t header;
} pcp_announce_request_t;

typedef union pcp_request {
  pcp_request_header_t header;
  pcp_map_request_t map;
  pcp_peer_request_t peer;
  pcp_announce_request_t announce;
} pcp_request_t;
