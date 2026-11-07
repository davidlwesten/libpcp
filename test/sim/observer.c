#include "observer.h"

#include <unity.h>

static sim_observed_mapping_t *find_mapping(sim_observer_t *observer,
                                            pcp_handle_t handle) {
  // the latest one, a handle could come back once its mapping is gone
  for (size_t i = observer->mapping_count; i > 0; --i) {
    if (observer->mappings[i - 1].handle == handle) {
      return &observer->mappings[i - 1];
    }
  }

  TEST_FAIL_MESSAGE("unknown handle");
  return nullptr;
}

static sim_observed_mapping_t *
record_event(sim_observer_t *observer, pcp_handle_t handle, int type) {
  auto const mapping = find_mapping(observer, handle);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(SIM_OBSERVED_FINISHED, mapping->state,
                                "event for a failed or released handle");
  TEST_ASSERT_LESS_THAN_size_t_MESSAGE(SIM_OBSERVER_MAX_EVENTS,
                                       mapping->event_count,
                                       "too many events to keep track of");
  mapping->events[mapping->event_count++] = type;
  return mapping;
}

void sim_observer_start(sim_observer_t *observer) {
  observer->mapping_count = 0;
}

void sim_observer_on_request(sim_observer_t *observer,
                             pcp_handle_t handle,
                             pcp_protocol_t protocol,
                             port_t port) {
  TEST_ASSERT_LESS_THAN_size_t_MESSAGE(SIM_OBSERVER_MAX_MAPPINGS,
                                       observer->mapping_count,
                                       "too many mappings to keep track of");

  observer->mappings[observer->mapping_count++] = (sim_observed_mapping_t){
      .handle = handle,
      .protocol = protocol,
      .port = port,
      .state = SIM_OBSERVED_WAITING,
  };
}

void sim_observer_on_release(sim_observer_t *observer, pcp_handle_t handle) {
  find_mapping(observer, handle)->state = SIM_OBSERVED_FINISHED;
}

void sim_observer_on_event(sim_observer_t *observer, pcp_event_t const *event) {
  switch (event->type) {
  case PCP_EVENT_MAPPED: {
    auto const mapping =
        record_event(observer, event->mapped.handle, PCP_EVENT_MAPPED);
    if (mapping->state == SIM_OBSERVED_MAPPED) {
      TEST_ASSERT_NOT_EQUAL_MESSAGE(
          0, socket_address_cmp(&mapping->external, &event->mapped.external),
          "repeated MAPPED without a new address");
    }
    mapping->state = SIM_OBSERVED_MAPPED;
    mapping->external = event->mapped.external;
    break;
  }
  case PCP_EVENT_UNMAPPED: {
    auto const mapping =
        record_event(observer, event->unmapped.handle, PCP_EVENT_UNMAPPED);
    TEST_ASSERT_EQUAL_MESSAGE(SIM_OBSERVED_MAPPED, mapping->state,
                              "UNMAPPED for a mapping that wasn't mapped");
    mapping->state = SIM_OBSERVED_WAITING;
    break;
  }
  case PCP_EVENT_FAILED: {
    auto const mapping =
        record_event(observer, event->failed.handle, PCP_EVENT_FAILED);
    mapping->state = SIM_OBSERVED_FINISHED;
    mapping->failure = event->failed.result;
    break;
  }
  case PCP_EVENT_SEND:
  case PCP_EVENT_TIMEOUT:
    TEST_FAIL_MESSAGE("the client's loop handles sends and timeouts itself");
    break;
  }
}

sim_observed_mapping_t const *sim_observer_find(sim_observer_t const *observer,
                                                pcp_handle_t handle) {
  // finding doesn't change anything, it only needs the mutable type internally
  return find_mapping((sim_observer_t *)observer, handle);
}
