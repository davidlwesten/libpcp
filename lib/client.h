#pragma once

#include <pcp/pcp.h>

#include "transport.h"

typedef struct pcp_nonce {
  uint8_t data[12];
} pcp_nonce_t;

pcp_nonce_t pcp_nonce_create();

typedef struct pcp_mappings {
  size_t capacity;
  size_t size;
  pcp_mapping_t *data;
} pcp_mappings_t;

typedef struct pcp_server {
  pcp_mappings_t active;
} pcp_server_t;

typedef struct pcp_client_internal {
  pcp_client_t base;
  pcp_server_t server;
  transport_t transport;
} pcp_client_internal_t;
