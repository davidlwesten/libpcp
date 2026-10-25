#pragma once

#include <pcp/pcp.h>

#include "nonce.h"
#include "request.h"
#include "schedule.h"

bool pcp_nonce_generate(pcp_rng_t rng, pcp_nonce_t *out);

// Only changed by the pcp_mapping_enter_* functions in mapping.c, which replace
// it as a whole so no state inherits another state's data
typedef struct pcp_mapping_state {
  enum {
    PENDING,
    ACTIVE,
    LOST, // was ACTIVE, PCP_EVENT_UNMAPPED is yet to be emitted
    FAILED,
  } type;
  union {
    struct {
      pcp_schedule_t schedule;
    } pending;
  };
} pcp_mapping_state_t;

typedef struct pcp_mapping {
  struct pcp_mapping *next;
  pcp_request_t request;
  pcp_nonce_t nonce;
  pcp_mapping_state_t state;
  size_t count;
  pcp_option_t options[];
} pcp_mapping_t;

/**
 * @brief Initialise a freshly allocated mapping and start requesting it
 *
 * @param[out] mapping Room for the mapping and `count` options
 */
void pcp_mapping_init(pcp_mapping_t *mapping,
                      pcp_request_t request,
                      pcp_nonce_t nonce,
                      size_t count,
                      pcp_option_t const options[count]);

/**
 * @brief Request the mapping again because the server lost its state
 */
void pcp_mapping_reset(pcp_mapping_t *mapping);

bool pcp_mapping_update(pcp_mapping_t *mapping,
                        pcp_time_ms_t now,
                        pcp_config_t const *config,
                        pcp_event_t *out);

/**
 * @brief When the mapping next needs pcp_mapping_update, if ever
 */
pcp_optional_time_ms_t pcp_mapping_deadline(pcp_mapping_t const *mapping);
