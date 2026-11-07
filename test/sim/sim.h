#pragma once

#include <pcp/pcp.h>

#include "client.h"
#include "network.h"
#include "observer.h"
#include "server.h"

// A simulated world for testing the client end to end. This is what scenarios
// use, and it owns virtual time and connects the pieces:
//
// - client.h, a generic application loop around the library's public API
// - network.h, what carries datagrams between the client and the server
// - server.h, a PCP server with real leases
// - observer.h, which checks the application only hears what pcp.h promises
//
// A world only lives on the stack of sim_run_scenario, which also reports it
// and ends its session.

static constexpr sim_duration_ms_t SIM_SECOND = 1000;
static constexpr sim_duration_ms_t SIM_MINUTE = 60 * SIM_SECOND;
static constexpr sim_duration_ms_t SIM_HOUR = 60 * SIM_MINUTE;

static constexpr size_t SIM_MAX_TRACE = 1024;

/**
 * @brief One line of a scenario's timeline
 */
typedef struct sim_trace_entry {
  pcp_time_ms_t at;
  char const *what;
  uint64_t value;
  bool has_address;
  socket_address_t address;
} sim_trace_entry_t;

typedef struct sim {
  /// Where the client's randomness and the clock's starting point come from,
  /// and what a behavior's randomness should start from
  uint64_t seed;
  uint64_t rng_state;
  pcp_time_ms_t started;
  pcp_time_ms_t now;
  ip_address_t local;

  sim_client_t client;
  sim_observer_t observer;
  sim_server_t server;
  sim_network_t network;

  /// From the client's last run
  pcp_optional_time_ms_t deadline;
  size_t events_this_run;

  size_t trace_count;
  sim_trace_entry_t trace[SIM_MAX_TRACE];
} sim_t;

typedef void (*sim_scenario_t)(sim_t *sim);

/**
 * @brief Run a scenario once per seed, from 1 to `seeds`, each in a new world
 *
 * Prints the timeline of the first seed, and of a seed that fails, which is
 * also where it stops.
 */
void sim_run_scenario(sim_scenario_t scenario, uint64_t seeds);

void sim_start(sim_t *sim);

void sim_run_for(sim_t *sim, sim_duration_ms_t duration);

/**
 * @brief Change what happens to datagrams from now on, all of them arrive
 *        normally by default
 */
void sim_set_network_behavior(sim_t *sim, sim_network_behavior_t behavior);

socket_address_t sim_external(sim_t const *sim, pcp_handle_t handle);

sim_server_stats_t sim_stats(sim_t const *sim);

sim_network_stats_t sim_network_stats(sim_t const *sim);

/**
 * @brief Check the application thinks `handle` is mapped, and that the server
 *        agrees on the address
 */
void sim_assert_mapped(sim_t *sim, pcp_handle_t handle);

void sim_assert_no_lease(sim_t *sim, pcp_handle_t handle);

void sim_assert_no_events(sim_t const *sim, pcp_handle_t handle);

/**
 * @brief Check nothing is in flight and the client has nothing scheduled
 */
void sim_assert_idle(sim_t const *sim);

/**
 * @brief Check these are all the events `handle` got, not counting sends
 */
void sim_assert_events(sim_t const *sim,
                       pcp_handle_t handle,
                       size_t count,
                       int const events[count]);

#define SIM_ASSERT_EVENTS(sim, handle, ...)                                    \
  sim_assert_events((sim), (handle),                                           \
                    sizeof((int const[]){__VA_ARGS__}) / sizeof(int),          \
                    (int const[]){__VA_ARGS__})
