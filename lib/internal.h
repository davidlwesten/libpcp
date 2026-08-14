#pragma once

#include <pcp/pcp.h>

#include "socket.h"

typedef enum pcp_opcode {
  ANNOUNCE = 0,
  MAP,
  PEER,
} pcp_opcode_t;

typedef struct pcp_nonce {
  uint8_t data[12];
} pcp_nonce_t;

pcp_nonce_t pcp_nonce_create();

typedef struct pcp_client_internal {
  pcp_client_t base;
  socket_t socket;
} pcp_client_internal_t;

bool pcp_result_is_valid(pcp_result_t code);
bool pcp_protocol_is_valid(pcp_protocol_t protocol);
