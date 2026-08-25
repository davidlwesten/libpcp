#include "mapping.h"

static void mapping_state_pending(pcp_mapping_t *mapping,
                                  pcp_response_t const *response) {
  if (response->header.code == SUCCESS) {
    *mapping =
        (pcp_mapping_t){.active = {.base.state = ACTIVE,
                                   .base.request = mapping->base.request}};
  } else {
    *mapping = (pcp_mapping_t){
        .error = {.base.state = ACTIVE, .base.request = mapping->base.request}};
  }
}

static void mapping_state_active(pcp_mapping_t * /*mapping*/,
                                 pcp_response_t const * /*response*/) {}

static void mapping_state_error(pcp_mapping_t * /*state*/,
                                pcp_response_t const * /*response*/) {}

void pcp_mapping_update(pcp_mapping_t *mapping,
                        pcp_response_t const *response) {
  switch (mapping->base.state) {
  case PENDING:
    mapping_state_pending(mapping, response);
    break;
  case ACTIVE:
    mapping_state_active(mapping, response);
    break;
  case ERROR:
    mapping_state_error(mapping, response);
    break;
  }
}
