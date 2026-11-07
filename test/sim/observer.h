#pragma once

#include <pcp/pcp.h>

// Watches what the application is told and fails the test as soon as that
// breaks a promise from pcp.h. What it keeps per mapping is what the
// application would believe, for scenarios to compare against the server.

static constexpr size_t SIM_OBSERVER_MAX_MAPPINGS = 16;
static constexpr size_t SIM_OBSERVER_MAX_EVENTS = 32;

typedef struct sim_observed_mapping {
  pcp_handle_t handle;
  pcp_protocol_t protocol;
  port_t port;
  enum {
    SIM_OBSERVED_WAITING, // requested or unmapped, so without an address
    SIM_OBSERVED_MAPPED,
    SIM_OBSERVED_FINISHED, // failed or released, no more events allowed
  } state;
  socket_address_t external;
  /// From PCP_EVENT_FAILED
  pcp_result_t failure;
  size_t event_count;
  int events[SIM_OBSERVER_MAX_EVENTS];
} sim_observed_mapping_t;

typedef struct sim_observer {
  size_t mapping_count;
  sim_observed_mapping_t mappings[SIM_OBSERVER_MAX_MAPPINGS];
} sim_observer_t;

void sim_observer_start(sim_observer_t *observer);

void sim_observer_on_request(sim_observer_t *observer,
                             pcp_handle_t handle,
                             pcp_protocol_t protocol,
                             port_t port);

void sim_observer_on_release(sim_observer_t *observer, pcp_handle_t handle);

/**
 * @brief The application got this event, which can't be a send or a timeout
 */
void sim_observer_on_event(sim_observer_t *observer, pcp_event_t const *event);

/**
 * @returns What's known about `handle`, failing the test if it was never handed
 *          out
 */
sim_observed_mapping_t const *sim_observer_find(sim_observer_t const *observer,
                                                pcp_handle_t handle);
