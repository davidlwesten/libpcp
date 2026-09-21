#pragma once

#include <pcp/transport.h>

#include <inttypes.h>
#include <stddef.h>

typedef enum pcp_opcode : uint8_t {
  ANNOUNCE = 0,
  MAP,
  PEER,
} pcp_opcode_t;

typedef struct pcp_nonce {
  uint8_t data[12];
} pcp_nonce_t;
