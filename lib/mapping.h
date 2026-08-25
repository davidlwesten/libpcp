#pragma once

#include "packet.h"
#include "response.h"

typedef enum pcp_mapping_state {
  PENDING,
  ACTIVE,
  ERROR,
} pcp_mapping_state_t;

typedef union pcp_mapping pcp_mapping_t;

typedef void (*pcp_mapping_state_update_t)(pcp_mapping_t *mapping,
                                           pcp_response_t const *response);

typedef struct pcp_mapping_state_base {
  pcp_mapping_state_t state;
  packet_t *request;
} pcp_mapping_state_base_t;

typedef struct pcp_mapping_state_pending {
  pcp_mapping_state_base_t base;
} pcp_mapping_state_pending_t;

typedef struct pcp_mapping_state_active {
  pcp_mapping_state_base_t base;
} pcp_mapping_state_active_t;

typedef struct pcp_mapping_state_error {
  pcp_mapping_state_base_t base;
} pcp_mapping_state_error_t;

typedef union pcp_mapping {
  pcp_mapping_state_base_t base;
  pcp_mapping_state_pending_t pending;
  pcp_mapping_state_active_t active;
  pcp_mapping_state_error_t error;
} pcp_mapping_t;

void pcp_mapping_update(pcp_mapping_t *mapping, pcp_response_t const *response);
