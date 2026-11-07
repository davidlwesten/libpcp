#include "sim.h"

#include <inttypes.h>
#include <stdio.h>
#include <unity.h>

// more events than this at one point in time means the client is spinning
static constexpr size_t SIM_MAX_EVENTS_PER_RUN = 1024;
static constexpr sim_duration_ms_t SIM_LATENCY = 10;

static_assert(PCP_PACKET_MAX_SIZE <= SIM_NETWORK_MAX_DATAGRAM_SIZE);

static bool sim_rng_fill(void *ctx, size_t size, uint8_t data[size]) {
  sim_t *sim = ctx;
  for (size_t i = 0; i < size; ++i) {
    data[i] = (uint8_t)sim_random(&sim->rng_state);
  }
  return true;
}

static void sim_trace_address(sim_t *sim,
                              char const *what,
                              uint64_t value,
                              socket_address_t const *address) {
  auto const entry = &sim->trace[sim->trace_count++ % SIM_MAX_TRACE];
  *entry = (sim_trace_entry_t){
      .at = sim->now,
      .what = what,
      .value = value,
  };
  if (address) {
    entry->has_address = true;
    entry->address = *address;
  }
}

static void sim_trace(sim_t *sim, char const *what, uint64_t value) {
  sim_trace_address(sim, what, value, nullptr);
}

static void sim_print_trace(sim_t const *sim) {
  printf("\nseed %" PRIu64 ", timeline in seconds since the start:\n",
         sim->seed);

  auto const count =
      sim->trace_count < SIM_MAX_TRACE ? sim->trace_count : SIM_MAX_TRACE;
  if (count < sim->trace_count) {
    printf("  (%zu older entries dropped)\n", sim->trace_count - count);
  }

  for (size_t i = sim->trace_count - count; i < sim->trace_count; ++i) {
    auto const entry = &sim->trace[i % SIM_MAX_TRACE];
    auto const elapsed = entry->at - sim->started;
    printf("  %8" PRIu64 ".%03" PRIu64 "  %s %" PRIu64, elapsed / 1000,
           elapsed % 1000, entry->what, entry->value);
    if (entry->has_address) {
      char address[SOCKET_ADDRESS_STRING_LENGTH];
      socket_address_to_string(sizeof(address), address, &entry->address);
      printf(" at %s", address);
    }
    printf("\n");
  }
}

static void sim_count_event(sim_t *sim) {
  TEST_ASSERT_LESS_THAN_size_t_MESSAGE(
      SIM_MAX_EVENTS_PER_RUN, sim->events_this_run,
      "the client keeps producing events without time passing");
  ++sim->events_this_run;
}

// the client's io, connecting it to the rest of the world

static void
sim_on_client_send(void *ctx, size_t size, uint8_t const data[size]) {
  sim_t *sim = ctx;
  sim_count_event(sim);
  sim_trace(sim, "client sent, bytes", size);
  TEST_ASSERT_LESS_OR_EQUAL_size_t_MESSAGE(PCP_PACKET_MAX_SIZE, size,
                                           "datagram over 1100 bytes");
  if (!sim_network_send(&sim->network, sim->now, SIM_ENDPOINT_SERVER, size,
                        data)) {
    sim_trace(sim, "network lost the client's datagram, bytes", size);
  }
}

static void sim_on_client_event(void *ctx, pcp_event_t const *event) {
  sim_t *sim = ctx;
  sim_count_event(sim);

  switch (event->type) {
  case PCP_EVENT_MAPPED:
    sim_trace_address(sim, "application got MAPPED, handle",
                      event->mapped.handle, &event->mapped.external);
    break;
  case PCP_EVENT_UNMAPPED:
    sim_trace(sim, "application got UNMAPPED, handle", event->unmapped.handle);
    break;
  case PCP_EVENT_FAILED:
    sim_trace(sim, "application got FAILED, result", event->failed.result);
    break;
  case PCP_EVENT_SEND:
  case PCP_EVENT_TIMEOUT:
    break;
  }

  sim_observer_on_event(&sim->observer, event);
}

static void sim_on_client_requested(void *ctx,
                                    pcp_handle_or_error_t result,
                                    pcp_protocol_t protocol,
                                    port_t port) {
  sim_t *sim = ctx;
  TEST_ASSERT_TRUE_MESSAGE(result.has_value, "the map request failed");
  sim_observer_on_request(&sim->observer, result.value, protocol, port);
  sim_trace(sim, "application requested mapping, handle", result.value);
}

static void sim_on_client_released(void *ctx, pcp_handle_t handle) {
  sim_t *sim = ctx;
  sim_trace(sim, "application released mapping, handle", handle);
  sim_observer_on_release(&sim->observer, handle);
}

static void
sim_send_to_client(sim_t *sim, size_t size, uint8_t const data[size]) {
  if (!sim_network_send(&sim->network, sim->now, SIM_ENDPOINT_CLIENT, size,
                        data)) {
    sim_trace(sim, "network lost the server's datagram, bytes", size);
  }
}

static void sim_deliver_to_server(sim_t *sim, sim_datagram_t const *datagram) {
  auto const reply = sim_server_on_datagram(&sim->server, sim->now, sim->local,
                                            datagram->size, datagram->data);
  sim_trace(sim, "server got request, bytes", datagram->size);
  sim_trace(sim, "server responded, result", reply.result);
  sim_send_to_client(sim, reply.size, reply.data);
}

static void sim_deliver_due(sim_t *sim) {
  sim_datagram_t datagram;
  while (sim_network_receive(&sim->network, sim->now, &datagram)) {
    switch (datagram.to) {
    case SIM_ENDPOINT_SERVER:
      sim_deliver_to_server(sim, &datagram);
      break;
    case SIM_ENDPOINT_CLIENT:
      pcp_session_on_datagram(sim->client.session, sim->now, datagram.size,
                              datagram.data);
      break;
    }
  }
}

static void sim_run_client(sim_t *sim) {
  sim->events_this_run = 0;
  sim->deadline = sim_client_run(&sim->client, sim->now);
  if (sim->deadline.has_value) {
    TEST_ASSERT_GREATER_THAN_UINT64_MESSAGE(
        sim->now, sim->deadline.value, "the deadline has to be in the future");
  }
}

// the first moment something can happen, `until` at the latest
static pcp_time_ms_t sim_next_time(sim_t const *sim, pcp_time_ms_t until) {
  auto next = until;
  if (sim->deadline.has_value && sim->deadline.value < next) {
    next = sim->deadline.value;
  }

  pcp_time_ms_t arrival = 0;
  if (sim_network_next_arrival(&sim->network, &arrival) && arrival < next) {
    next = arrival;
  }
  return next;
}

// A failed assertion jumps back here. This has to be its own function because
// what a function changes in its own locals after calling setjmp is lost after
// the jump, and the world has to survive it.
static bool sim_run_protected(sim_t *sim, sim_scenario_t scenario) {
  if (TEST_PROTECT()) {
    scenario(sim);
    return true;
  }
  return false;
}

void sim_run_scenario(sim_scenario_t scenario, uint64_t seeds) {
  for (uint64_t seed = 1; seed <= seeds; ++seed) {
    sim_t sim = {.seed = seed};
    auto const passed = sim_run_protected(&sim, scenario);
    if (!passed || seed == 1) {
      sim_print_trace(&sim);
    }
    if (sim.client.session) {
      pcp_session_end(sim.client.session);
    }
    if (!passed) {
      return;
    }
  }
}

void sim_start(sim_t *sim) {
  sim->rng_state = sim->seed;

  // any origin works for the client's clock, see pcp_time_ms_t
  sim->started = sim_random(&sim->rng_state) >> 24;
  sim->now = sim->started;

  sim->local = ip_address_from_octets(10, 0, 0, 2);
  sim_network_start(&sim->network, SIM_LATENCY);
  sim_server_start(&sim->server, ip_address_from_octets(203, 0, 113, 7),
                   sim->now);
  sim_observer_start(&sim->observer);

  TEST_ASSERT_TRUE(sim_client_start(&sim->client, sim->local,
                                    (pcp_rng_t){
                                        .fill = sim_rng_fill,
                                        .ctx = sim,
                                    },
                                    (sim_io_t){
                                        .send = sim_on_client_send,
                                        .on_event = sim_on_client_event,
                                        .on_requested = sim_on_client_requested,
                                        .on_released = sim_on_client_released,
                                        .ctx = sim,
                                    }));
}

void sim_run_for(sim_t *sim, sim_duration_ms_t duration) {
  auto const until = sim->now + duration;
  while (true) {
    sim_deliver_due(sim);
    sim_run_client(sim);
    if (sim->now == until) {
      return;
    }
    sim->now = sim_next_time(sim, until);
  }
}

void sim_set_network_behavior(sim_t *sim, sim_network_behavior_t behavior) {
  sim_network_set_behavior(&sim->network, behavior);
}

socket_address_t sim_external(sim_t const *sim, pcp_handle_t handle) {
  auto const mapping = sim_observer_find(&sim->observer, handle);
  TEST_ASSERT_EQUAL_MESSAGE(SIM_OBSERVED_MAPPED, mapping->state,
                            "the application has no address for this handle");
  return mapping->external;
}

sim_server_stats_t sim_stats(sim_t const *sim) { return sim->server.stats; }

sim_network_stats_t sim_network_stats(sim_t const *sim) {
  return sim->network.stats;
}

void sim_assert_mapped(sim_t *sim, pcp_handle_t handle) {
  auto const mapping = sim_observer_find(&sim->observer, handle);
  TEST_ASSERT_EQUAL_MESSAGE(SIM_OBSERVED_MAPPED, mapping->state,
                            "the application doesn't think it's mapped");

  auto const lease = sim_server_find_lease(&sim->server, sim->now,
                                           mapping->protocol, mapping->port);
  TEST_ASSERT_NOT_NULL_MESSAGE(lease, "the server has no lease for it");
  TEST_ASSERT_EQUAL_MESSAGE(
      0, socket_address_cmp(&lease->external, &mapping->external),
      "the application and server disagree on the address");
}

void sim_assert_no_lease(sim_t *sim, pcp_handle_t handle) {
  auto const mapping = sim_observer_find(&sim->observer, handle);
  TEST_ASSERT_NULL_MESSAGE(sim_server_find_lease(&sim->server, sim->now,
                                                 mapping->protocol,
                                                 mapping->port),
                           "the server still has a lease for it");
}

void sim_assert_no_events(sim_t const *sim, pcp_handle_t handle) {
  TEST_ASSERT_EQUAL_size_t_MESSAGE(
      0, sim_observer_find(&sim->observer, handle)->event_count,
      "the application already heard about this mapping");
}

void sim_assert_idle(sim_t const *sim) {
  TEST_ASSERT_EQUAL_size_t_MESSAGE(0, sim_network_in_flight(&sim->network),
                                   "datagrams in flight");
  TEST_ASSERT_FALSE_MESSAGE(sim->deadline.has_value,
                            "the client still has something scheduled");
}

// for messages, as the names in pcp.h without their prefix
static char const *sim_event_name(int type) {
  switch (type) {
  case PCP_EVENT_SEND:
    return "SEND";
  case PCP_EVENT_MAPPED:
    return "MAPPED";
  case PCP_EVENT_UNMAPPED:
    return "UNMAPPED";
  case PCP_EVENT_FAILED:
    return "FAILED";
  case PCP_EVENT_TIMEOUT:
    return "TIMEOUT";
  default:
    return "an unknown event";
  }
}

// appends the events' names to `text`, which holds `used` of its `size` bytes
static size_t sim_describe_events(size_t count,
                                  int const events[count],
                                  size_t size,
                                  char text[size],
                                  size_t used) {
  if (count == 0) {
    return used + (size_t)snprintf(text + used, size - used, "nothing");
  }

  for (size_t i = 0; i < count && used < size; ++i) {
    used += (size_t)snprintf(text + used, size - used, "%s%s",
                             i == 0 ? "" : ", ", sim_event_name(events[i]));
  }
  return used;
}

void sim_assert_events(sim_t const *sim,
                       pcp_handle_t handle,
                       size_t count,
                       int const events[count]) {
  auto const mapping = sim_observer_find(&sim->observer, handle);

  auto same = count == mapping->event_count;
  for (size_t i = 0; same && i < count; ++i) {
    same = events[i] == mapping->events[i];
  }
  if (same) {
    return;
  }

  char message[256];
  size_t used =
      (size_t)snprintf(message, sizeof(message), "expected the events ");
  used = sim_describe_events(count, events, sizeof(message), message, used);
  if (used < sizeof(message)) {
    used +=
        (size_t)snprintf(message + used, sizeof(message) - used, " but got ");
  }
  if (used < sizeof(message)) {
    sim_describe_events(mapping->event_count, mapping->events, sizeof(message),
                        message, used);
  }
  TEST_FAIL_MESSAGE(message);
}
