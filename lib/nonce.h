#pragma once

#include <stdint.h>

/**
 * @brief Ties all requests for a mapping (renewals too) to the server's
 *        responses
 */
typedef struct pcp_nonce {
  uint8_t data[12];
} pcp_nonce_t;
