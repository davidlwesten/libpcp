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

// Only set by the pcp_mapping_enter_* functions, which overwrite all of it so
// nothing carries over from one state to the next
typedef struct pcp_mapping_state {
  enum {
    PENDING,
    ACTIVE,
    FAILED,    // refused, FAILED event not sent yet
    RELEASING, // released, sending the delete request
    RELEASED,  // done, waiting to be freed
  } type;
  union {
    struct {
      pcp_schedule_t schedule;
    } pending;
    struct {
      pcp_mapping_lease_t lease;
      pcp_schedule_t schedule; // renewals
    } active;
    struct {
      pcp_result_t result;
    } failed;
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
  // Address from the last MAPPED event, cleared by UNMAPPED. Only used by
  // pcp_mapping_report() to figure out which event to send next.
  optional_socket_address_t last_mapped;
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
 * @brief Handle a server response. Responses for other mappings are ignored.
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
 * @brief Release the mapping
 *
 * No more events are reported for it. It becomes RELEASED once the delete is
 * done, or right away if there's nothing to delete.
 */
void pcp_mapping_release(pcp_mapping_t *mapping);

/**
 * @brief Bring the mapping up to date with `now`
 *
 * Handles timeouts first (like an expired lease), then reports changes to the
 * application, then sends whatever is due. Call until it returns false.
 *
 * @param[out] out The event for the application, if any
 *
 * @returns Whether there was an event
 */
bool pcp_mapping_update(pcp_mapping_t *mapping,
                        pcp_time_ms_t now,
                        pcp_config_t const *config,
                        pcp_event_t *out);

/**
 * @brief When the mapping next needs pcp_mapping_update, if ever
 */
pcp_optional_time_ms_t pcp_mapping_deadline(pcp_mapping_t const *mapping);
