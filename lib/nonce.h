#pragma once

#include <stdint.h>

// identifies a mapping across all requests for it (e.g. renewals) and the
// server's responses to them
typedef struct pcp_nonce {
  uint8_t data[12];
} pcp_nonce_t;
