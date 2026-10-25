#include "session.h"

#include <assert.h>

bool pcp_mapping_update(pcp_mapping_t *mapping,
                        time_t /*now*/,
                        pcp_event_t *out) {
  assert(mapping);
  assert(out);

  switch (mapping->state) {
  case PENDING:
    *out = (pcp_event_t){
        .type = PCP_EVENT_WRITE,
    };
    return true;
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

pcp_mapping_t pcp_session_add_mapping(pcp_session_t *session,
                                      pcp_mapping_t *mapping) {
  assert(session);
  assert(mapping);
  assert(mapping->next == nullptr);

  if (!session->mappings) {
    session->mappings = mapping;
  } else {
    pcp_mapping_t *iter = session->mappings;
    while (iter->next) {
      iter = iter->next;
    }

    iter->next = mapping;
  }

  return (pcp_mapping_t)mapping;
}
