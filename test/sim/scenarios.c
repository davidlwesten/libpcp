#include <unity.h>

#include "runner.h"
#include "sim.h"

// Scenarios for the client in a simulated world (see sim.h). Each one is a
// short script: set up the world, act like an application, let time pass, and
// check what the application and the server ended up with.
//
//   SCENARIO(test_sim_survives_an_outage) {
//     auto const handle = start_mapped(sim);
//
//     sim_set_network_behavior(sim, SIM_NETWORK_DOWN);
//     sim_run_for(sim, 10 * SIM_MINUTE);
//     sim_set_network_behavior(sim, SIM_NETWORK_UP);
//
//     sim_run_for(sim, SIM_HOUR);
//     sim_assert_mapped(sim, handle);
//   }
//
// and a RUN_TEST for it at the bottom of this file. It runs once for each of
// SEEDS seeds, and prints the timeline of the first seed and of any that fail.
// To watch one, run e.g. ./build/test/sim/sim_tests test_sim_full_lifecycle
//
// What a scenario has to work with:
//
// - The world: sim_start, or start_mapped to begin with a granted mapping.
//   sim_run_for lets time pass.
// - The application: map, or sim_client_map for the library's whole result,
//   and sim_client_release.
// - The network: sim_set_network_behavior with SIM_RANDOM_FAULTS,
//   SIM_NETWORK_DOWN, SIM_NETWORK_UP, or a hook of the scenario's own.
// - Checks: sim_assert_mapped, sim_assert_no_lease, sim_assert_no_events,
//   sim_assert_idle, SIM_ASSERT_EVENTS, and the counts in sim_stats and
//   sim_network_stats.
//
// Set the network's behavior right after sim_start for it to apply from the
// start, and use sim->seed for anything random, so that every seed is
// different.

// jitter, nonces and network faults come from the seed, so go through a few
static constexpr uint64_t SEEDS = 16;

static constexpr pcp_time_s_t LIFETIME = 3600;
static constexpr port_t PORT = 1234;

// Renewing starts 1/2 into a lease (RFC 6887 section 11.2.1), so this is before
// the first renewal is due
static constexpr sim_duration_ms_t BEFORE_RENEWAL = 25 * SIM_MINUTE;

// retransmissions back off up to 1024s plus 10% (section 8.1.1), so a client
// that's been unable to reach the server tries again within this
static constexpr sim_duration_ms_t LONGEST_RETRY = 20 * SIM_MINUTE;

// a scenario that runs once per seed, in a new world each time
#define SCENARIO(name)                                                         \
  static void name##_scenario(sim_t *sim);                                     \
  static void name() { sim_run_scenario(name##_scenario, SEEDS); }             \
  static void name##_scenario(sim_t *sim)

static void assert_same_address(socket_address_t expected,
                                socket_address_t actual) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, socket_address_cmp(&expected, &actual),
                                "the external address changed");
}

// asks for a mapping like the application would, a request that fails has
// already failed the scenario
static pcp_handle_t
map(sim_t *sim, pcp_time_s_t lifetime, pcp_protocol_t protocol, port_t port) {
  return sim_client_map(&sim->client, lifetime, protocol, port).value;
}

// a mapping the server has granted, with some time left until its renewal
static pcp_handle_t map_and_settle(sim_t *sim) {
  auto const handle = map(sim, LIFETIME, UDP, PORT);
  sim_run_for(sim, SIM_SECOND);
  sim_assert_mapped(sim, handle);
  return handle;
}

static pcp_handle_t start_mapped(sim_t *sim) {
  sim_start(sim);
  return map_and_settle(sim);
}

// Network

SCENARIO(test_sim_full_lifecycle) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);

  // the same mapping all day, without the application hearing about it
  sim_run_for(sim, 24 * SIM_HOUR);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);

  // renewing 1/2 to 5/8 into every lease (section 11.2.1), and nothing is lost
  // so every request is a first attempt
  auto stats = sim_stats(sim);
  TEST_ASSERT_EQUAL_size_t(0, stats.expired);
  TEST_ASSERT_EQUAL_size_t(1, stats.created);
  TEST_ASSERT_GREATER_OR_EQUAL_size_t(24 * 3600 / (LIFETIME * 5 / 8),
                                      stats.renewed);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(24 * 3600 / (LIFETIME / 2), stats.renewed);
  TEST_ASSERT_EQUAL_size_t(1 + stats.renewed, stats.requests);

  sim_client_release(&sim->client, handle);
  sim_run_for(sim, SIM_SECOND);
  sim_assert_no_lease(sim, handle);
  sim_assert_idle(sim);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);

  stats = sim_stats(sim);
  TEST_ASSERT_EQUAL_size_t(1, stats.deleted);
  TEST_ASSERT_EQUAL_size_t(0, stats.expired);
  TEST_ASSERT_EQUAL_size_t(2 + stats.renewed, stats.requests);
}

SCENARIO(test_sim_heavy_loss) {
  sim_start(sim);
  sim_set_network_behavior(sim,
                           SIM_RANDOM_FAULTS(sim->seed, .loss_percent = 30));
  auto const handle = map(sim, LIFETIME, UDP, PORT);

  // a lease can run out at this rate, but never for good
  sim_run_for(sim, 24 * SIM_HOUR);
  TEST_ASSERT_GREATER_THAN_size_t(0, sim_network_stats(sim).lost);

  sim_set_network_behavior(sim, (sim_network_behavior_t){0});
  sim_run_for(sim, LONGEST_RETRY);
  sim_assert_mapped(sim, handle);

  // the release can be lost too, but the lease runs out either way
  sim_set_network_behavior(sim,
                           SIM_RANDOM_FAULTS(sim->seed, .loss_percent = 30));
  sim_client_release(&sim->client, handle);
  sim_run_for(sim, 2 * SIM_HOUR);
  sim_assert_no_lease(sim, handle);
  sim_assert_idle(sim);
}

SCENARIO(test_sim_duplicates_and_reordering) {
  sim_start(sim);
  sim_set_network_behavior(
      sim,
      SIM_RANDOM_FAULTS(sim->seed, .duplicate_percent = 20, .jitter = 500));
  auto const handle = map_and_settle(sim);
  auto const external = sim_external(sim, handle);

  sim_run_for(sim, 24 * SIM_HOUR);
  TEST_ASSERT_GREATER_THAN_size_t(0, sim_network_stats(sim).duplicated);

  // within the epoch's slack, so nothing here looks like a server restart
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
  TEST_ASSERT_EQUAL_size_t(1, sim_stats(sim).created);
  TEST_ASSERT_EQUAL_size_t(0, sim_stats(sim).expired);
}

SCENARIO(test_sim_outage_before_mapping) {
  sim_start(sim);
  sim_set_network_behavior(sim, SIM_NETWORK_DOWN);
  auto const handle = map(sim, LIFETIME, UDP, PORT);

  sim_run_for(sim, SIM_HOUR);
  sim_assert_no_events(sim, handle);

  // backing off, between about 10 and 15 attempts in the first hour
  auto const attempts = sim_network_stats(sim).lost;
  TEST_ASSERT_GREATER_OR_EQUAL_size_t(8, attempts);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(15, attempts);

  sim_set_network_behavior(sim, SIM_NETWORK_UP);
  sim_run_for(sim, LONGEST_RETRY);
  sim_assert_mapped(sim, handle);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
}

SCENARIO(test_sim_outage_shorter_than_lease) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);

  // covers when the first renewal is due, but the lease has time left
  sim_run_for(sim, BEFORE_RENEWAL);
  sim_set_network_behavior(sim, SIM_NETWORK_DOWN);
  sim_run_for(sim, 20 * SIM_MINUTE);
  sim_set_network_behavior(sim, SIM_NETWORK_UP);
  TEST_ASSERT_GREATER_THAN_size_t(0, sim_network_stats(sim).lost);

  // retries before the lease runs out
  sim_run_for(sim, SIM_HOUR);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
  TEST_ASSERT_EQUAL_size_t(0, sim_stats(sim).expired);
}

SCENARIO(test_sim_outage_longer_than_lease) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);

  sim_set_network_behavior(sim, SIM_NETWORK_DOWN);
  sim_run_for(sim, 2 * SIM_HOUR);
  sim_set_network_behavior(sim, SIM_NETWORK_UP);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED, PCP_EVENT_UNMAPPED);

  // the old address is still free, and the client asks for it back
  sim_run_for(sim, LONGEST_RETRY);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED, PCP_EVENT_UNMAPPED,
                    PCP_EVENT_MAPPED);
  TEST_ASSERT_EQUAL_size_t(1, sim_stats(sim).expired);
}

// Hostile traffic

// a copy of the first response gets stuck in the network until the first
// renewal is nearly due
static sim_fate_t hold_copy_of_first_response(void *ctx,
                                              sim_endpoint_t to,
                                              size_t /*size*/,
                                              uint8_t const /*data*/[]) {
  bool *held = ctx;
  if (*held || to != SIM_ENDPOINT_CLIENT) {
    return (sim_fate_t){0};
  }

  *held = true;
  return (sim_fate_t){.duplicated = true, .duplicate_delay = BEFORE_RENEWAL};
}

SCENARIO(test_sim_recovers_from_stale_response) {
  bool held = false;
  sim_start(sim);
  sim_set_network_behavior(sim, (sim_network_behavior_t){
                                    .on_send = hold_copy_of_first_response,
                                    .ctx = &held,
                                });
  auto const handle = map_and_settle(sim);
  auto const external = sim_external(sim, handle);

  // however the client reads its old epoch, it has to end up with the same
  // mapping
  sim_run_for(sim, BEFORE_RENEWAL + SIM_SECOND);
  TEST_ASSERT_EQUAL_size_t(1, sim_network_stats(sim).duplicated);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
}

void scenario_tests() {
  RUN_TEST(test_sim_full_lifecycle);
  RUN_TEST(test_sim_heavy_loss);
  RUN_TEST(test_sim_duplicates_and_reordering);
  RUN_TEST(test_sim_outage_before_mapping);
  RUN_TEST(test_sim_outage_shorter_than_lease);
  RUN_TEST(test_sim_outage_longer_than_lease);
  RUN_TEST(test_sim_recovers_from_stale_response);
}
