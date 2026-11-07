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
//   sim_run_for lets time pass, sim_sleep lets it pass without the client.
// - The application: map, or sim_client_map for the library's whole result,
//   and sim_client_release.
// - The network: sim_set_network_behavior with SIM_RANDOM_FAULTS,
//   SIM_NETWORK_DOWN, SIM_NETWORK_UP, or a hook of the scenario's own.
// - The server: sim_set_server_behavior with SIM_SERVER_REFUSES,
//   SIM_SERVER_GRANTS_AT_MOST, SIM_SERVER_NATPMP_ONLY, SIM_SERVER_NORMAL, or a
//   hook. sim_restart_server, sim_inject and sim_inject_forged_response for
//   one-off events.
// - Checks: sim_assert_mapped, sim_assert_no_lease, sim_assert_failed,
//   sim_assert_no_events, sim_assert_idle, SIM_ASSERT_EVENTS, and the counts in
//   sim_stats and sim_network_stats.
//
// Set behaviors right after sim_start for them to apply from the start, and use
// sim->seed for anything random, so that every seed is different.

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

// Server restarts

SCENARIO(test_sim_restart_with_announce) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);
  sim_run_for(sim, SIM_MINUTE);

  sim_restart_server(sim, (sim_restart_t){.announce = true});

  // clients wait a random 0-5s (section 14.1.3)
  sim_run_for(sim, 6 * SIM_SECOND);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED, PCP_EVENT_UNMAPPED,
                    PCP_EVENT_MAPPED);
}

SCENARIO(test_sim_restart_found_out_from_renewal) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);

  // so much uptime is lost that the next response's epoch gives it away, but
  // nothing tells the client until then
  sim_run_for(sim, BEFORE_RENEWAL);
  sim_restart_server(sim, (sim_restart_t){.announce = false});
  sim_run_for(sim, SIM_MINUTE);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);

  sim_run_for(sim, SIM_HOUR);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED, PCP_EVENT_UNMAPPED,
                    PCP_EVENT_MAPPED);
  TEST_ASSERT_EQUAL_size_t(0, sim_stats(sim).expired);
}

SCENARIO(test_sim_restart_within_epoch_tolerance) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);

  // The uptime lost is within the 1/16 the clocks may drift apart by the next
  // response (RFC 6887 section 8.5), so the client can't tell. Its renewal
  // recreates the lease anyway, so it never needs to.
  sim_run_for(sim, SIM_MINUTE);
  sim_restart_server(sim, (sim_restart_t){.announce = false});

  sim_run_for(sim, SIM_HOUR);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
  TEST_ASSERT_EQUAL_size_t(2, sim_stats(sim).created);
}

SCENARIO(test_sim_restart_announce_lost) {
  auto const handle = start_mapped(sim);

  sim_run_for(sim, BEFORE_RENEWAL);
  sim_set_network_behavior(sim, SIM_NETWORK_DOWN);
  sim_restart_server(sim, (sim_restart_t){.announce = true});
  sim_run_for(sim, SIM_MINUTE);
  sim_set_network_behavior(sim, SIM_NETWORK_UP);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);

  sim_run_for(sim, SIM_HOUR);
  sim_assert_mapped(sim, handle);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED, PCP_EVENT_UNMAPPED,
                    PCP_EVENT_MAPPED);
}

SCENARIO(test_sim_restart_with_new_address) {
  auto const handle = start_mapped(sim);
  auto const old_external = sim_external(sim, handle);
  sim_run_for(sim, SIM_MINUTE);

  sim_restart_server(sim,
                     (sim_restart_t){.announce = true, .new_address = true});
  sim_run_for(sim, 6 * SIM_SECOND);

  // the old one can't be granted anymore, so the application hears the new one
  sim_assert_mapped(sim, handle);
  auto const new_external = sim_external(sim, handle);
  TEST_ASSERT_NOT_EQUAL_INT(0,
                            socket_address_cmp(&old_external, &new_external));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED, PCP_EVENT_UNMAPPED,
                    PCP_EVENT_MAPPED);
}

SCENARIO(test_sim_restart_during_release) {
  auto const handle = start_mapped(sim);
  sim_run_for(sim, SIM_MINUTE);

  // the delete is still on its way to the server
  sim_client_release(&sim->client, handle);
  sim_run_for(sim, 5);
  sim_restart_server(sim, (sim_restart_t){.announce = true});

  sim_run_for(sim, SIM_MINUTE);
  sim_assert_no_lease(sim, handle);
  sim_assert_idle(sim);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
}

// Server decisions

SCENARIO(test_sim_server_grants_less_than_requested) {
  static constexpr pcp_time_s_t granted = 600;

  sim_start(sim);
  sim_set_server_behavior(sim, SIM_SERVER_GRANTS_AT_MOST(granted));
  auto const handle = map(sim, LIFETIME, UDP, PORT);
  sim_run_for(sim, SIM_SECOND);
  sim_assert_mapped(sim, handle);

  // renewing on the server's schedule instead of the requested one
  sim_run_for(sim, 24 * SIM_HOUR);
  sim_assert_mapped(sim, handle);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);

  auto const stats = sim_stats(sim);
  TEST_ASSERT_EQUAL_size_t(0, stats.expired);
  TEST_ASSERT_GREATER_OR_EQUAL_size_t(24 * 3600 / (granted * 5 / 8),
                                      stats.renewed);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(24 * 3600 / (granted / 2), stats.renewed);
}

SCENARIO(test_sim_temporary_error_on_request) {
  static constexpr pcp_time_s_t retry_after = 30;

  sim_start(sim);
  sim_set_server_behavior(sim, SIM_SERVER_REFUSES(NO_RESOURCES, retry_after));
  auto const handle = map(sim, LIFETIME, UDP, PORT);

  // doesn't try again until the error's lifetime is up
  sim_run_for(sim, 20 * SIM_SECOND);
  sim_assert_no_events(sim, handle);
  TEST_ASSERT_EQUAL_size_t(1, sim_stats(sim).requests);

  sim_set_server_behavior(sim, SIM_SERVER_NORMAL);
  sim_run_for(sim, 20 * SIM_SECOND);
  sim_assert_mapped(sim, handle);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
}

SCENARIO(test_sim_temporary_error_on_renewal) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);

  sim_run_for(sim, BEFORE_RENEWAL);
  sim_set_server_behavior(sim, SIM_SERVER_REFUSES(NO_RESOURCES, 600));
  sim_run_for(sim, 15 * SIM_MINUTE);
  TEST_ASSERT_GREATER_THAN_size_t(0, sim_stats(sim).refused);

  // the lease is still good, so the application doesn't need to hear about it
  sim_set_server_behavior(sim, SIM_SERVER_NORMAL);
  sim_run_for(sim, 15 * SIM_MINUTE);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
  TEST_ASSERT_EQUAL_size_t(0, sim_stats(sim).expired);
}

SCENARIO(test_sim_permanent_error_on_request) {
  sim_start(sim);
  sim_set_server_behavior(sim, SIM_SERVER_REFUSES(NOT_AUTHORIZED, 0));
  auto const handle = map(sim, LIFETIME, UDP, PORT);

  sim_run_for(sim, SIM_MINUTE);
  sim_assert_failed(sim, handle, NOT_AUTHORIZED);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_FAILED);
  sim_assert_no_lease(sim, handle);
  sim_assert_idle(sim);
  TEST_ASSERT_EQUAL_size_t(1, sim_stats(sim).requests);
}

SCENARIO(test_sim_permanent_error_on_renewal) {
  auto const handle = start_mapped(sim);

  sim_run_for(sim, BEFORE_RENEWAL);
  sim_set_server_behavior(sim, SIM_SERVER_REFUSES(NOT_AUTHORIZED, 0));
  sim_run_for(sim, 15 * SIM_MINUTE);

  sim_assert_failed(sim, handle, NOT_AUTHORIZED);
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED, PCP_EVENT_FAILED);
  sim_assert_idle(sim);
}

SCENARIO(test_sim_natpmp_only_router) {
  sim_start(sim);
  sim_set_server_behavior(sim, SIM_SERVER_NATPMP_ONLY);
  auto const first = map(sim, LIFETIME, UDP, PORT);
  auto const second = map(sim, LIFETIME, TCP, PORT);

  // one answer is enough to give up on all of them, for the application to fall
  // back to NAT-PMP
  sim_run_for(sim, SIM_MINUTE);
  sim_assert_failed(sim, first, UNSUPP_VERSION);
  sim_assert_failed(sim, second, UNSUPP_VERSION);
  sim_assert_idle(sim);
}

SCENARIO(test_sim_same_port_requested_twice) {
  auto const first = start_mapped(sim);

  // the server binds a mapping to the nonce that created it
  auto const second = map(sim, LIFETIME, UDP, PORT);
  sim_run_for(sim, SIM_MINUTE);
  sim_assert_failed(sim, second, NOT_AUTHORIZED);

  sim_run_for(sim, SIM_HOUR);
  sim_assert_mapped(sim, first);
  SIM_ASSERT_EVENTS(sim, first, PCP_EVENT_MAPPED);
}

// The client's machine

SCENARIO(test_sim_sleep_shorter_than_lease) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);

  sim_run_for(sim, BEFORE_RENEWAL);
  sim_sleep(sim, 20 * SIM_MINUTE);
  auto const requests = sim_stats(sim).requests;

  // a single late renewal, not everything it missed
  sim_run_for(sim, SIM_SECOND);
  TEST_ASSERT_EQUAL_size_t(requests + 1, sim_stats(sim).requests);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
}

SCENARIO(test_sim_sleep_longer_than_lease) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);

  sim_sleep(sim, 2 * SIM_HOUR);
  auto const requests = sim_stats(sim).requests;

  sim_run_for(sim, SIM_SECOND);
  TEST_ASSERT_EQUAL_size_t(requests + 1, sim_stats(sim).requests);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED, PCP_EVENT_UNMAPPED,
                    PCP_EVENT_MAPPED);
}

// Hostile traffic

SCENARIO(test_sim_ignores_malformed_datagrams) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);
  auto const requests = sim_stats(sim).requests;

  // too short for a header, then version 2 with the request bit, without a
  // body, with an unknown opcode and with a size that's no multiple of 4
  sim_inject(sim, 3, (uint8_t const[]){2, 0x81, 0});
  sim_inject(sim, 60, (uint8_t const[60]){2, 0x01});
  sim_inject(sim, 4, (uint8_t const[]){2, 0x81, 0, 0});
  sim_inject(sim, 60, (uint8_t const[60]){2, 0xFF});
  sim_inject(sim, 62, (uint8_t const[62]){2, 0x81});

  sim_run_for(sim, SIM_SECOND);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
  TEST_ASSERT_EQUAL_size_t(requests, sim_stats(sim).requests);
}

SCENARIO(test_sim_ignores_other_version_without_response_bit) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);

  // looks like UNSUPP_VERSION from another version, but isn't a response, which
  // has to be dropped before looking at any of that (RFC 6887 section 8.3)
  sim_inject(sim, 4, (uint8_t const[]){0x47, 0x00, 0x00, UNSUPP_VERSION});

  sim_run_for(sim, SIM_SECOND);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
}

SCENARIO(test_sim_ignores_forged_response) {
  auto const handle = start_mapped(sim);
  auto const external = sim_external(sim, handle);

  sim_inject_forged_response(sim, handle);

  sim_run_for(sim, SIM_SECOND);
  sim_assert_mapped(sim, handle);
  assert_same_address(external, sim_external(sim, handle));
  SIM_ASSERT_EVENTS(sim, handle, PCP_EVENT_MAPPED);
}

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

// Many mappings

SCENARIO(test_sim_many_mappings) {
  static constexpr size_t count = 6;

  sim_start(sim);
  pcp_handle_t handles[count];
  for (size_t i = 0; i < count; ++i) {
    handles[i] =
        map(sim, (pcp_time_s_t)(600 * (i + 1)), UDP, (port_t)(1000 + i));
  }

  sim_run_for(sim, SIM_HOUR);
  for (size_t i = 0; i < count; ++i) {
    sim_assert_mapped(sim, handles[i]);
    SIM_ASSERT_EVENTS(sim, handles[i], PCP_EVENT_MAPPED);

    auto const external = sim_external(sim, handles[i]);
    for (size_t j = 0; j < i; ++j) {
      auto const other = sim_external(sim, handles[j]);
      TEST_ASSERT_NOT_EQUAL_INT(0, socket_address_cmp(&external, &other));
    }
  }

  // releasing some leaves the others alone
  for (size_t i = 0; i < count; i += 2) {
    sim_client_release(&sim->client, handles[i]);
  }
  sim_run_for(sim, SIM_SECOND);

  sim_run_for(sim, 24 * SIM_HOUR);
  for (size_t i = 0; i < count; ++i) {
    if (i % 2 == 0) {
      sim_assert_no_lease(sim, handles[i]);
    } else {
      sim_assert_mapped(sim, handles[i]);
    }
    SIM_ASSERT_EVENTS(sim, handles[i], PCP_EVENT_MAPPED);
  }
  TEST_ASSERT_EQUAL_size_t(count / 2, sim_stats(sim).deleted);
  TEST_ASSERT_EQUAL_size_t(0, sim_stats(sim).expired);
}

void scenario_tests() {
  RUN_TEST(test_sim_full_lifecycle);
  RUN_TEST(test_sim_heavy_loss);
  RUN_TEST(test_sim_duplicates_and_reordering);
  RUN_TEST(test_sim_outage_before_mapping);
  RUN_TEST(test_sim_outage_shorter_than_lease);
  RUN_TEST(test_sim_outage_longer_than_lease);
  RUN_TEST(test_sim_restart_with_announce);
  RUN_TEST(test_sim_restart_found_out_from_renewal);
  RUN_TEST(test_sim_restart_within_epoch_tolerance);
  RUN_TEST(test_sim_restart_announce_lost);
  RUN_TEST(test_sim_restart_with_new_address);
  RUN_TEST(test_sim_restart_during_release);
  RUN_TEST(test_sim_server_grants_less_than_requested);
  RUN_TEST(test_sim_temporary_error_on_request);
  RUN_TEST(test_sim_temporary_error_on_renewal);
  RUN_TEST(test_sim_permanent_error_on_request);
  RUN_TEST(test_sim_permanent_error_on_renewal);
  RUN_TEST(test_sim_natpmp_only_router);
  RUN_TEST(test_sim_same_port_requested_twice);
  RUN_TEST(test_sim_sleep_shorter_than_lease);
  RUN_TEST(test_sim_sleep_longer_than_lease);
  RUN_TEST(test_sim_ignores_malformed_datagrams);
  RUN_TEST(test_sim_ignores_other_version_without_response_bit);
  RUN_TEST(test_sim_ignores_forged_response);
  RUN_TEST(test_sim_recovers_from_stale_response);
  RUN_TEST(test_sim_many_mappings);
}
