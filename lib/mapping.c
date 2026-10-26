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
  mapping->state = (pcp_mapping_state_t){.type = PENDING};
  pcp_schedule_backoff(&mapping->state.pending.schedule, PCP_BACKOFF_DEFAULT,
                       not_before);
}

static void pcp_mapping_enter_confirmed(pcp_mapping_t *mapping,
                                        pcp_mapping_lease_t lease) {
  // from ACTIVE if a renewal came back with a different external address
  assert(mapping->state.type == PENDING || mapping->state.type == ACTIVE);
  mapping->state = (pcp_mapping_state_t){
      .type = CONFIRMED,
      .confirmed = {.lease = lease},
  };
}

static void pcp_mapping_enter_active(pcp_mapping_t *mapping,
                                     pcp_mapping_lease_t lease,
                                     pcp_rng_t rng) {
  // also used for a successful renewal that kept the external address
  assert(mapping->state.type == CONFIRMED || mapping->state.type == ACTIVE);
  mapping->state = (pcp_mapping_state_t){
      .type = ACTIVE,
      .active = {.lease = lease},
  };
  pcp_schedule_renewal(&mapping->state.active.schedule, lease.granted,
                       lease.lifetime, rng);
}

static void pcp_mapping_enter_lost(pcp_mapping_t *mapping,
                                   pcp_time_ms_t not_before) {
  assert(mapping->state.type == ACTIVE);
  mapping->state = (pcp_mapping_state_t){
      .type = LOST,
      .lost = {.not_before = not_before},
  };
}

static void pcp_mapping_enter_refused(pcp_mapping_t *mapping,
                                      pcp_result_t result) {
  assert(mapping->state.type == PENDING || mapping->state.type == ACTIVE);
  mapping->state = (pcp_mapping_state_t){
      .type = REFUSED,
      .refused = {.result = result},
  };
}

static void pcp_mapping_enter_failed(pcp_mapping_t *mapping) {
  assert(mapping->state.type == PENDING || mapping->state.type == REFUSED);
  mapping->state = (pcp_mapping_state_t){.type = FAILED};
}

void pcp_mapping_init(pcp_mapping_t *mapping,
                      pcp_request_t request,
                      pcp_nonce_t nonce,
                      size_t count,
                      pcp_option_t const options[count]) {
  assert(mapping);
  assert(count == 0 || options);

  *mapping = (pcp_mapping_t){
      .next = nullptr,
      .request = request,
      .nonce = nonce,
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

  // a renewal that kept the address has nothing to tell the application
  if (mapping->state.type == ACTIVE &&
      socket_address_cmp(&lease.external,
                         &mapping->state.active.lease.external) == 0) {
    pcp_mapping_enter_active(mapping, lease, rng);
  } else {
    pcp_mapping_enter_confirmed(mapping, lease);
  }
}

static void pcp_mapping_on_temporary_error(pcp_mapping_t *mapping,
                                           pcp_response_t const *response,
                                           pcp_time_ms_t now) {
  assert(mapping->state.type == PENDING || mapping->state.type == ACTIVE);

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
  default:
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

  // only PENDING and ACTIVE mappings send requests, so only they can be
  // waiting for a response
  switch (mapping->state.type) {
  case PENDING:
  case ACTIVE:
    break;
  case CONFIRMED:
  case LOST:
  case REFUSED:
  case FAILED:
    return;
  }

  if (!pcp_mapping_matches(mapping, response)) {
    return;
  }

  if (response->result == SUCCESS) {
    pcp_mapping_on_success(mapping, response, now, rng);
  } else if (pcp_result_is_temporary(response->result)) {
    pcp_mapping_on_temporary_error(mapping, response, now);
  } else {
    pcp_mapping_enter_refused(mapping, response->result);
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

static bool pcp_mapping_update_pending(pcp_mapping_t *mapping,
                                       pcp_time_ms_t now,
                                       pcp_config_t const *config,
                                       pcp_event_t *out) {
  auto const schedule = &mapping->state.pending.schedule;

  switch (pcp_schedule_poll(schedule, now)) {
  case PCP_SCHEDULE_WAIT:
    return false;
  case PCP_SCHEDULE_DUE:
    return pcp_mapping_send(mapping, schedule, now, config, out);
  case PCP_SCHEDULE_EXHAUSTED:
    // only bounded schedules run out, i.e. deleting a mapping the application
    // already released, so there's nobody to notify
    pcp_mapping_enter_failed(mapping);
    return false;
  }
}

static bool pcp_mapping_update_confirmed(pcp_mapping_t *mapping,
                                         pcp_config_t const *config,
                                         pcp_event_t *out) {
  auto const lease = mapping->state.confirmed.lease;
  pcp_mapping_enter_active(mapping, lease, config->rng);

  *out = (pcp_event_t){
      .type = PCP_EVENT_MAPPED,
      .mapped =
          {
              .handle = (pcp_handle_t)mapping,
              .external = lease.external,
          },
  };
  return true;
}

static bool pcp_mapping_update_lost(pcp_mapping_t *mapping, pcp_event_t *out) {
  // request the mapping again, like a new one
  pcp_mapping_enter_pending(mapping, mapping->state.lost.not_before);

  *out = (pcp_event_t){
      .type = PCP_EVENT_UNMAPPED,
      .unmapped =
          {
              .handle = (pcp_handle_t)mapping,
          },
  };
  return true;
}

static bool pcp_mapping_update_active(pcp_mapping_t *mapping,
                                      pcp_time_ms_t now,
                                      pcp_config_t const *config,
                                      pcp_event_t *out) {
  auto const schedule = &mapping->state.active.schedule;

  switch (pcp_schedule_poll(schedule, now)) {
  case PCP_SCHEDULE_WAIT:
    return false;
  case PCP_SCHEDULE_DUE:
    return pcp_mapping_send(mapping, schedule, now, config, out);
  case PCP_SCHEDULE_EXHAUSTED:
    // no renewal succeeded before the lease expired
    pcp_mapping_enter_lost(mapping, 0);
    return pcp_mapping_update_lost(mapping, out);
  }
}

static bool pcp_mapping_update_refused(pcp_mapping_t *mapping,
                                       pcp_event_t *out) {
  auto const result = mapping->state.refused.result;
  pcp_mapping_enter_failed(mapping);

  *out = (pcp_event_t){
      .type = PCP_EVENT_FAILED,
      .failed =
          {
              .handle = (pcp_handle_t)mapping,
              .result = result,
          },
  };
  return true;
}

void pcp_mapping_reset(pcp_mapping_t *mapping, pcp_time_ms_t not_before) {
  assert(mapping);

  // RFC 6887 §8.5: the mapping is gone (or about to be), so request it again
  // rather than continuing any backoff
  switch (mapping->state.type) {
  case PENDING:
  case CONFIRMED:
    // the application never learned about a CONFIRMED mapping's address, so
    // there's nothing to take back
    pcp_mapping_enter_pending(mapping, not_before);
    break;
  case ACTIVE:
    // the application has to learn its external address is gone first, the
    // request follows once it's PENDING again
    pcp_mapping_enter_lost(mapping, not_before);
    break;
  case LOST:
  case REFUSED:
  case FAILED:
    break;
  }
}

bool pcp_mapping_update(pcp_mapping_t *mapping,
                        pcp_time_ms_t now,
                        pcp_config_t const *config,
                        pcp_event_t *out) {
  assert(mapping);
  assert(config);
  assert(out);

  switch (mapping->state.type) {
  case PENDING:
    return pcp_mapping_update_pending(mapping, now, config, out);
  case CONFIRMED:
    return pcp_mapping_update_confirmed(mapping, config, out);
  case ACTIVE:
    return pcp_mapping_update_active(mapping, now, config, out);
  case LOST:
    return pcp_mapping_update_lost(mapping, out);
  case REFUSED:
    return pcp_mapping_update_refused(mapping, out);
  case FAILED:
    return false;
  }
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
  case CONFIRMED:
  case LOST:
  case REFUSED:
    // these still owe the application an event, so they're due right away.
    // pcp_session_poll never leaves them behind, this is just for safety
    return (pcp_optional_time_ms_t){.has_value = true, .value = 0};
  case FAILED:
    return (pcp_optional_time_ms_t){.has_value = false, .value = 0};
  }
}

bool pcp_nonce_generate(pcp_rng_t rng, pcp_nonce_t *out) {
  assert(rng.fill);
  assert(out);

  return rng.fill(rng.ctx, sizeof(out->data), out->data);
}
