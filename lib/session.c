#include "session.h"

#include <assert.h>

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
