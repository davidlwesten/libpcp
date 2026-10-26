#pragma once

#include <pcp/pcp.h>

#include "nonce.h"
#include "request.h"
#include "response.h"
#include "schedule.h"

bool pcp_nonce_generate(pcp_rng_t rng, pcp_nonce_t *out);

// What the server granted
typedef struct pcp_mapping_lease {
  socket_address_t external;
  pcp_time_ms_t granted;
  pcp_time_ms_t lifetime;
} pcp_mapping_lease_t;

// Only changed by the pcp_mapping_enter_* functions in mapping.c, which replace
// it as a whole so no state inherits another state's data
typedef struct pcp_mapping_state {
  enum {
    PENDING,
    CONFIRMED, // granted, PCP_EVENT_MAPPED is yet to be emitted
    ACTIVE,
    LOST,    // was ACTIVE, PCP_EVENT_UNMAPPED is yet to be emitted
    REFUSED, // refused, PCP_EVENT_FAILED is yet to be emitted
    FAILED,
    RELEASING, // released by the application, asking the server to delete it
    RELEASED,  // released by the application, for the session to free
  } type;
  union {
    struct {
      pcp_schedule_t schedule;
    } pending;
    struct {
      pcp_mapping_lease_t lease;
    } confirmed;
    struct {
      pcp_mapping_lease_t lease;
      pcp_schedule_t schedule; // renewals
    } active;
    struct {
      pcp_time_ms_t not_before; // when to request the mapping again
    } lost;
    struct {
      pcp_result_t result;
    } refused;
    struct {
      pcp_schedule_t schedule;
    } releasing;
  };
} pcp_mapping_state_t;

typedef struct pcp_mapping {
  struct pcp_mapping *next;
  pcp_handle_t handle;
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
                      pcp_handle_t handle,
                      pcp_request_t request,
                      pcp_nonce_t nonce,
                      size_t count,
                      pcp_option_t const options[count]);

/**
 * @brief Handle a response from the server, ignoring it unless it belongs to
 *        the mapping
 */
void pcp_mapping_on_response(pcp_mapping_t *mapping,
                             pcp_response_t const *response,
                             pcp_time_ms_t now,
                             pcp_rng_t rng);

/**
 * @brief Request the mapping again because the server lost its state
 *
 * @param[in] not_before When to send the request, 0 for straight away
 */
void pcp_mapping_reset(pcp_mapping_t *mapping, pcp_time_ms_t not_before);

/**
 * @brief Give the mapping up on behalf of the application
 *
 * No further events refer to the mapping. It's RELEASED once nothing is left to
 * do for it, possibly straight away.
 */
void pcp_mapping_release(pcp_mapping_t *mapping);

bool pcp_mapping_update(pcp_mapping_t *mapping,
                        pcp_time_ms_t now,
                        pcp_config_t const *config,
                        pcp_event_t *out);

/**
 * @brief When the mapping next needs pcp_mapping_update, if ever
 */
pcp_optional_time_ms_t pcp_mapping_deadline(pcp_mapping_t const *mapping);
