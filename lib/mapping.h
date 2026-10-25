#pragma once

#include <pcp/pcp.h>

#include "nonce.h"
#include "request.h"
#include "schedule.h"

bool pcp_nonce_generate(pcp_rng_t rng, pcp_nonce_t *out);

typedef struct pcp_mapping {
  struct pcp_mapping *next;
  pcp_request_t request;
  pcp_nonce_t nonce;
  enum {
    PENDING,
    ACTIVE,
    LOST, // was ACTIVE, PCP_EVENT_UNMAPPED is yet to be emitted
    FAILED,
  } state;
  pcp_schedule_t schedule;
  size_t count;
  pcp_option_t options[];
} pcp_mapping_t;

/**
 * @brief Request the mapping again because the server lost its state
 */
void pcp_mapping_on_server_reset(pcp_mapping_t *mapping);

bool pcp_mapping_update(pcp_mapping_t *mapping,
                        pcp_time_ms_t now,
                        pcp_config_t const *config,
                        pcp_event_t *out);
