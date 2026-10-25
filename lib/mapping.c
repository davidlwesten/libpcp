#include "mapping.h"
#include "wire.h"

#include <assert.h>
#include <string.h>

// Entering a state replaces `mapping->state` as a whole, so every field the
// enter function doesn't set is zeroed rather than left over from before

static void pcp_mapping_enter_pending(pcp_mapping_t *mapping) {
  // also used to restart an already PENDING mapping's schedule
  mapping->state = (pcp_mapping_state_t){.type = PENDING};
  pcp_schedule_start(&mapping->state.pending.schedule, PCP_BACKOFF_DEFAULT, 0);
}

static void pcp_mapping_enter_lost(pcp_mapping_t *mapping) {
  assert(mapping->state.type == ACTIVE);
  mapping->state = (pcp_mapping_state_t){.type = LOST};
}

static void pcp_mapping_enter_failed(pcp_mapping_t *mapping) {
  assert(mapping->state.type == PENDING);
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

  pcp_mapping_enter_pending(mapping);
}

static bool pcp_mapping_update_pending(pcp_mapping_t *mapping,
                                       pcp_time_ms_t now,
                                       pcp_config_t const *config,
                                       pcp_event_t *out) {
  auto const schedule = &mapping->state.pending.schedule;

  switch (pcp_schedule_poll(schedule, now)) {
  case PCP_SCHEDULE_WAIT:
    return false;
  case PCP_SCHEDULE_DUE: {
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
  case PCP_SCHEDULE_EXHAUSTED:
    // only bounded schedules run out, i.e. deleting a mapping the application
    // already released, so there's nobody to notify
    pcp_mapping_enter_failed(mapping);
    return false;
  }
}

static bool pcp_mapping_update_lost(pcp_mapping_t *mapping, pcp_event_t *out) {
  // request the mapping again, like a new one
  pcp_mapping_enter_pending(mapping);

  *out = (pcp_event_t){
      .type = PCP_EVENT_UNMAPPED,
      .unmapped =
          {
              .handle = (pcp_handle_t)mapping,
          },
  };
  return true;
}

void pcp_mapping_reset(pcp_mapping_t *mapping) {
  assert(mapping);

  // RFC 6887 §8.5: the mapping is gone (or about to be), so request it again
  // straight away rather than continuing any backoff
  switch (mapping->state.type) {
  case PENDING:
    pcp_mapping_enter_pending(mapping);
    break;
  case ACTIVE:
    // the application has to learn its external address is gone first, the
    // request follows once it's PENDING again
    pcp_mapping_enter_lost(mapping);
    break;
  case LOST:
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
  case ACTIVE:
    return false;
  case LOST:
    return pcp_mapping_update_lost(mapping, out);
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
  case LOST:
  case FAILED:
    return (pcp_optional_time_ms_t){.has_value = false, .value = 0};
  }
}

bool pcp_nonce_generate(pcp_rng_t rng, pcp_nonce_t *out) {
  assert(rng.random);
  assert(out);

  return rng.random(rng.ctx, sizeof(out->data), out->data);
}
