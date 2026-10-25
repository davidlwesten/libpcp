#include "mapping.h"
#include "wire.h"

#include <assert.h>

static bool pcp_mapping_update_pending(pcp_mapping_t *mapping,
                                       pcp_time_ms_t now,
                                       pcp_config_t const *config,
                                       pcp_event_t *out) {
  switch (pcp_schedule_poll(&mapping->schedule, now)) {
  case PCP_SCHEDULE_WAIT:
    return false;
  case PCP_SCHEDULE_DUE: {
    auto const size = pcp_packet_write_request(
        (view_t){.size = config->buffer.size, .data = config->buffer.data},
        &mapping->request, &mapping->nonce, &config->local, mapping->count,
        mapping->options);

    // the application sends the request in response to this event, so this is
    // the transmission as far as the schedule is concerned
    pcp_schedule_advance(&mapping->schedule, now, config->rng);
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
    mapping->state = FAILED;
    return false;
  }
}

static bool pcp_mapping_update_lost(pcp_mapping_t *mapping, pcp_event_t *out) {
  // request the mapping again, like a new one
  mapping->state = PENDING;
  pcp_schedule_start(&mapping->schedule, PCP_BACKOFF_DEFAULT, 0);

  *out = (pcp_event_t){
      .type = PCP_EVENT_UNMAPPED,
      .unmapped =
          {
              .handle = (pcp_handle_t)mapping,
          },
  };
  return true;
}

void pcp_mapping_on_server_reset(pcp_mapping_t *mapping) {
  assert(mapping);

  // RFC 6887 §8.5: the mapping is gone (or about to be), so request it again
  // straight away rather than continuing any backoff
  switch (mapping->state) {
  case PENDING:
    pcp_schedule_start(&mapping->schedule, PCP_BACKOFF_DEFAULT, 0);
    break;
  case ACTIVE:
    // the application has to learn its external address is gone first, the
    // request follows once it's PENDING again
    mapping->state = LOST;
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

  switch (mapping->state) {
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

bool pcp_nonce_generate(pcp_rng_t rng, pcp_nonce_t *out) {
  assert(rng.random);
  assert(out);

  return rng.random(rng.ctx, sizeof(out->data), out->data);
}
