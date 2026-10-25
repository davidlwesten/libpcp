#pragma once

#include <pcp/pcp.h>

#include "mapping.h"

typedef struct pcp_session {
  pcp_config_t config;
  pcp_mapping_t *mappings;
} pcp_session_t;

pcp_handle_t pcp_session_add_mapping(pcp_session_t *session,
                                     pcp_mapping_t *mapping);
void pcp_session_remove_mapping(pcp_session_t *session, pcp_mapping_t mapping);
