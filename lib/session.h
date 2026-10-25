#pragma once

#include <pcp/pcp.h>

#include "request.h"

typedef struct pcp_nonce {
  uint8_t data[12];
} pcp_nonce_t;

bool pcp_session_generate_nonce(pcp_session_t const *session, pcp_nonce_t *out);

typedef struct pcp_mapping {
  struct pcp_mapping *next;
  pcp_request_t request;
  pcp_nonce_t nonce;
  enum {
    PENDING,
    ACTIVE,
    FAILED,
  } state;
  size_t count;
  pcp_option_t options[];
} pcp_mapping_t;

bool pcp_mapping_update(pcp_mapping_t *mapping,
                        pcp_time_ms_t now,
                        pcp_event_t *out);

typedef struct pcp_session {
  pcp_config_t config;
  pcp_mapping_t *mappings;
} pcp_session_t;

pcp_handle_t pcp_session_add_mapping(pcp_session_t *session,
                                     pcp_mapping_t *mapping);
void pcp_session_remove_mapping(pcp_session_t *session, pcp_mapping_t mapping);
