#pragma once

#include <inttypes.h>
#include <stddef.h>

typedef struct view {
  size_t size;
  uint8_t *data;
} view_t;

typedef struct const_view {
  size_t size;
  uint8_t const *data;
} const_view_t;

typedef enum pcp_opcode : uint8_t {
  ANNOUNCE = 0,
  MAP,
  PEER,
} pcp_opcode_t;

typedef struct pcp_nonce {
  uint8_t data[12];
} pcp_nonce_t;
