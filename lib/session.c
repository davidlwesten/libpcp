#include "session.h"
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
        &mapping->request, &config->local, mapping->count, mapping->options);

    // the application sends the request in response to this event, so this is
    // the transmission as far as the schedule is concerned
    pcp_schedule_advance(&mapping->schedule, now, config->rng);
    *out = (pcp_event_t){
        .type = PCP_EVENT_SEND,
        .send =
            {
                .handle = (pcp_handle_t)mapping,
                .size = size,
                .data = config->buffer.data,
            },
    };
    return true;
  }
  case PCP_SCHEDULE_EXHAUSTED:
    mapping->state = FAILED;
    return false;
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
  case FAILED:
    return false;
  }
}

bool pcp_session_generate_nonce(pcp_session_t const *session,
                                pcp_nonce_t *out) {
  assert(session);
  assert(out);

  return session->config.rng.random(session->config.rng.ctx, sizeof(out->data),
                                    out->data);
}

pcp_handle_t pcp_session_add_mapping(pcp_session_t *session,
                                     pcp_mapping_t *mapping) {
  assert(session);
  assert(mapping);
  assert(mapping->next == nullptr);

  if (!session->mappings) {
    session->mappings = mapping;
  } else {
    auto iter = session->mappings;
    while (iter->next) {
      iter = iter->next;
    }

    iter->next = mapping;
  }

  return (pcp_handle_t)mapping;
}
