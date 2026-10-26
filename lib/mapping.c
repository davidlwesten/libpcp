#include "mapping.h"
#include "wire.h"

#include <assert.h>
#include <string.h>

static pcp_time_ms_t pcp_seconds(pcp_time_s_t seconds) {
  return (pcp_time_ms_t)seconds * 1'000;
}

// Entering a state replaces `mapping->state` as a whole, so every field the
// enter function doesn't set is zeroed rather than left over from before

static void pcp_mapping_enter_pending(pcp_mapping_t *mapping,
                                      pcp_time_ms_t not_before) {
  // also used to restart an already PENDING mapping's schedule
  assert(mapping->state.type == PENDING || mapping->state.type == ACTIVE);
  mapping->state = (pcp_mapping_state_t){.type = PENDING};
  pcp_schedule_backoff(&mapping->state.pending.schedule, PCP_BACKOFF_DEFAULT,
                       not_before);
}

static void pcp_mapping_enter_active(pcp_mapping_t *mapping,
                                     pcp_mapping_lease_t lease,
                                     pcp_rng_t rng) {
  // also used for a successful renewal
  assert(mapping->state.type == PENDING || mapping->state.type == ACTIVE);
  mapping->state = (pcp_mapping_state_t){
      .type = ACTIVE,
      .active = {.lease = lease},
  };
  pcp_schedule_renewal(&mapping->state.active.schedule, lease.granted,
                       lease.lifetime, rng);
}

static void pcp_mapping_enter_failed(pcp_mapping_t *mapping,
                                     pcp_result_t result) {
  assert(mapping->state.type == PENDING || mapping->state.type == ACTIVE);
  mapping->state = (pcp_mapping_state_t){
      .type = FAILED,
      .failed = {.result = result},
  };
}

static void pcp_mapping_enter_releasing(pcp_mapping_t *mapping) {
  // RFC 6887 §15: only MAP mappings can be deleted, by requesting them with a
  // lifetime of 0 and no suggested external address
  assert(mapping->request.type == MAP);
  assert(mapping->state.type == PENDING || mapping->state.type == ACTIVE);

  mapping->request.lifetime = 0;
  mapping->request.map.external = (socket_address_t){0};

  mapping->state = (pcp_mapping_state_t){.type = RELEASING};
  pcp_schedule_backoff(&mapping->state.releasing.schedule, PCP_BACKOFF_RELEASE,
                       0);
}

static void pcp_mapping_enter_released(pcp_mapping_t *mapping) {
  assert(mapping->state.type != RELEASED);
  mapping->state = (pcp_mapping_state_t){.type = RELEASED};
}

void pcp_mapping_init(pcp_mapping_t *mapping,
                      pcp_handle_t handle,
                      pcp_request_t request,
                      pcp_nonce_t nonce,
                      size_t count,
                      pcp_option_t const options[count]) {
  assert(mapping);
  assert(count == 0 || options);

  *mapping = (pcp_mapping_t){
      .next = nullptr,
      .handle = handle,
      .request = request,
      .nonce = nonce,
      .state = {.type = PENDING},
      .last_mapped = {.has_value = false, .value = {}},
      .count = count,
  };
  if (count) {
    memcpy(mapping->options, options, sizeof(pcp_option_t) * count);
  }

  pcp_mapping_enter_pending(mapping, 0);
}

// RFC 6887 §11.4 and §12.4
static bool pcp_mapping_matches_body(pcp_mapping_t const *mapping,
                                     pcp_nonce_t const *nonce,
                                     pcp_protocol_t protocol,
                                     port_t port) {
  return memcmp(nonce->data, mapping->nonce.data, sizeof(nonce->data)) == 0 &&
         protocol == mapping->request.protocol && port == mapping->request.port;
}

static bool pcp_mapping_matches(pcp_mapping_t const *mapping,
                                pcp_response_t const *response) {
  auto const request = &mapping->request;
  if (response->type != request->type) {
    return false;
  }

  switch (response->type) {
  case MAP:
    return pcp_mapping_matches_body(mapping, &response->map.nonce,
                                    response->map.protocol, response->map.port);
  case PEER:
    return pcp_mapping_matches_body(mapping, &response->peer.nonce,
                                    response->peer.protocol,
                                    response->peer.port) &&
           socket_address_cmp(&response->peer.peer, &request->peer.peer) == 0;
  case ANNOUNCE:
    return false;
  }
}

// later requests for the mapping (renewals, or requesting it again after it was
// lost) ask the server to keep what it granted
static void pcp_mapping_suggest(pcp_mapping_t *mapping,
                                socket_address_t external) {
  switch (mapping->request.type) {
  case MAP:
    mapping->request.map.external = external;
    break;
  case PEER:
    mapping->request.peer.external = external;
    break;
  case ANNOUNCE:
    assert(false && "mappings are never ANNOUNCE requests");
    break;
  }
}

// RFC 6887 §7.4: errors the server expects to go away by themselves, with the
// lifetime saying when to try again
static bool pcp_result_is_temporary(pcp_result_t result) {
  switch (result) {
  case NETWORK_FAILURE:
  case NO_RESOURCES:
  case USER_EX_QUOTA:
  case CANNOT_PROVIDE_EXTERNAL:
    return true;
  default:
    return false;
  }
}

static void pcp_mapping_on_success(pcp_mapping_t *mapping,
                                   pcp_response_t const *response,
                                   pcp_time_ms_t now,
                                   pcp_rng_t rng) {
  pcp_mapping_lease_t const lease = {
      .external = response->type == MAP ? response->map.external
                                        : response->peer.external,
      .granted = now,
      .lifetime = pcp_seconds(response->lifetime),
  };
  pcp_mapping_suggest(mapping, lease.external);
  pcp_mapping_enter_active(mapping, lease, rng);
}

static void pcp_mapping_on_temporary_error(pcp_mapping_t *mapping,
                                           pcp_response_t const *response,
                                           pcp_time_ms_t now) {
  auto const not_before = now + pcp_seconds(response->lifetime);

  switch (mapping->state.type) {
  case PENDING:
    pcp_mapping_enter_pending(mapping, not_before);
    break;
  case ACTIVE:
    // the lease is still valid, so keep it and renew later. If that's past its
    // expiry, the mapping is lost then
    pcp_schedule_defer(&mapping->state.active.schedule, not_before);
    break;
  case FAILED:
  case RELEASING:
  case RELEASED:
    unreachable();
  }
}

void pcp_mapping_on_response(pcp_mapping_t *mapping,
                             pcp_response_t const *response,
                             pcp_time_ms_t now,
                             pcp_rng_t rng) {
  assert(mapping);
  assert(response);
  assert(response->type == MAP || response->type == PEER);

  // only these send requests, so only they can be waiting for a response
  switch (mapping->state.type) {
  case PENDING:
  case ACTIVE:
  case RELEASING:
    break;
  case FAILED:
  case RELEASED:
    return;
  }

  if (!pcp_mapping_matches(mapping, response)) {
    return;
  }

  // whatever the result there's nothing more to do: after an error the server
  // asks not to repeat the request, so the lease just runs out
  if (mapping->state.type == RELEASING) {
    pcp_mapping_enter_released(mapping);
    return;
  }

  if (response->result == SUCCESS) {
    pcp_mapping_on_success(mapping, response, now, rng);
  } else if (pcp_result_is_temporary(response->result)) {
    pcp_mapping_on_temporary_error(mapping, response, now);
  } else {
    pcp_mapping_enter_failed(mapping, response->result);
  }
}

void pcp_mapping_reset(pcp_mapping_t *mapping, pcp_time_ms_t not_before) {
  assert(mapping);

  // RFC 6887 §8.5: the mapping is gone (or about to be), so request it again
  // rather than continuing any backoff
  switch (mapping->state.type) {
  case PENDING:
  case ACTIVE:
    pcp_mapping_enter_pending(mapping, not_before);
    break;
  case RELEASING:
    // the server already forgot what we're asking it to delete
    pcp_mapping_enter_released(mapping);
    break;
  case FAILED:
  case RELEASED:
    break;
  }
}

void pcp_mapping_release(pcp_mapping_t *mapping) {
  assert(mapping);
  assert(mapping->state.type != RELEASING && mapping->state.type != RELEASED);

  // RFC 6887 §12: PEER mappings can't be deleted, only left to expire
  if (mapping->request.type == PEER) {
    pcp_mapping_enter_released(mapping);
    return;
  }

  switch (mapping->state.type) {
  case PENDING:
    // the server may have granted a request even if its response hasn't
    // arrived, but not one that was never sent
    if (mapping->state.pending.schedule.count > 0) {
      pcp_mapping_enter_releasing(mapping);
    } else {
      pcp_mapping_enter_released(mapping);
    }
    break;
  case ACTIVE:
    pcp_mapping_enter_releasing(mapping);
    break;
  case FAILED:
    // refused, so there's nothing the server would still hold, and the
    // application gave up before hearing about it
    pcp_mapping_enter_released(mapping);
    break;
  case RELEASING:
  case RELEASED:
    unreachable();
  }
}

// the schedule of a state that sends requests
static pcp_schedule_t *pcp_mapping_schedule(pcp_mapping_t *mapping) {
  switch (mapping->state.type) {
  case PENDING:
    return &mapping->state.pending.schedule;
  case ACTIVE:
    return &mapping->state.active.schedule;
  case RELEASING:
    return &mapping->state.releasing.schedule;
  case FAILED:
  case RELEASED:
    return nullptr;
  }
}

static void pcp_mapping_on_exhausted(pcp_mapping_t *mapping) {
  switch (mapping->state.type) {
  case ACTIVE:
    // no renewal succeeded before the lease expired, so request it again
    pcp_mapping_enter_pending(mapping, 0);
    break;
  case RELEASING:
    // the server never answered, so the mapping expires at the end of its lease
    pcp_mapping_enter_released(mapping);
    break;
  case PENDING:
    assert(false && "PENDING mappings retry without bound");
    break;
  case FAILED:
  case RELEASED:
    unreachable();
  }
}

// tells the application about any change it hasn't heard about
static bool pcp_mapping_report(pcp_mapping_t *mapping, pcp_event_t *out) {
  auto const last_mapped = &mapping->last_mapped;

  switch (mapping->state.type) {
  case PENDING:
    if (!last_mapped->has_value) {
      return false;
    }

    *last_mapped = (optional_socket_address_t){.has_value = false, .value = {}};
    *out = (pcp_event_t){
        .type = PCP_EVENT_UNMAPPED,
        .unmapped = {.handle = mapping->handle},
    };
    return true;
  case ACTIVE: {
    auto const external = mapping->state.active.lease.external;
    if (last_mapped->has_value &&
        socket_address_cmp(&last_mapped->value, &external) == 0) {
      return false;
    }

    *last_mapped =
        (optional_socket_address_t){.has_value = true, .value = external};
    *out = (pcp_event_t){
        .type = PCP_EVENT_MAPPED,
        .mapped = {.handle = mapping->handle, .external = external},
    };
    return true;
  }
  case FAILED: {
    // the last event for the handle, so the mapping is done with
    auto const result = mapping->state.failed.result;
    pcp_mapping_enter_released(mapping);

    *out = (pcp_event_t){
        .type = PCP_EVENT_FAILED,
        .failed = {.handle = mapping->handle, .result = result},
    };
    return true;
  }
  case RELEASING:
  case RELEASED:
    // the application no longer holds the handle
    return false;
  }
}

// writes the mapping's request for the application to send
static bool pcp_mapping_send(pcp_mapping_t *mapping,
                             pcp_schedule_t *schedule,
                             pcp_time_ms_t now,
                             pcp_config_t const *config,
                             pcp_event_t *out) {
  auto const size = pcp_packet_write_request(
      (view_t){.size = config->buffer.size, .data = config->buffer.data},
      &mapping->request, &mapping->nonce, &config->local, mapping->count,
      mapping->options);

  // the application sends the request in response to this event, so this is
  // the transmission as far as the schedule is concerned
  pcp_schedule_advance(schedule, now, config->rng);
  *out = (pcp_event_t){
      .type = PCP_EVENT_SEND,
      .send =
          {
              .size = size,
              .data = config->buffer.data,
          },
  };
  return true;
}

bool pcp_mapping_update(pcp_mapping_t *mapping,
                        pcp_time_ms_t now,
                        pcp_config_t const *config,
                        pcp_event_t *out) {
  assert(mapping);
  assert(config);
  assert(out);

  // first whatever the passing of time decided, e.g. that a lease expired
  auto schedule = pcp_mapping_schedule(mapping);
  if (schedule && pcp_schedule_poll(schedule, now) == PCP_SCHEDULE_EXHAUSTED) {
    pcp_mapping_on_exhausted(mapping);
  }

  // then the application learns about any change, before a request goes out
  if (pcp_mapping_report(mapping, out)) {
    return true;
  }

  schedule = pcp_mapping_schedule(mapping);
  if (schedule && pcp_schedule_poll(schedule, now) == PCP_SCHEDULE_DUE) {
    return pcp_mapping_send(mapping, schedule, now, config, out);
  }

  return false;
}

pcp_optional_time_ms_t pcp_mapping_deadline(pcp_mapping_t const *mapping) {
  assert(mapping);

  switch (mapping->state.type) {
  case PENDING:
    return (pcp_optional_time_ms_t){
        .has_value = true,
        .value = pcp_schedule_deadline(&mapping->state.pending.schedule),
    };
  case ACTIVE:
    return (pcp_optional_time_ms_t){
        .has_value = true,
        .value = pcp_schedule_deadline(&mapping->state.active.schedule),
    };
  case RELEASING:
    return (pcp_optional_time_ms_t){
        .has_value = true,
        .value = pcp_schedule_deadline(&mapping->state.releasing.schedule),
    };
  case FAILED:
    // still owes the application an event, so it's due right away.
    // pcp_session_poll never leaves it behind, this is just for safety
    return (pcp_optional_time_ms_t){.has_value = true, .value = 0};
  case RELEASED:
    return (pcp_optional_time_ms_t){.has_value = false, .value = 0};
  }
}

bool pcp_nonce_generate(pcp_rng_t rng, pcp_nonce_t *out) {
  assert(rng.fill);
  assert(out);

  return rng.fill(rng.ctx, sizeof(out->data), out->data);
}
