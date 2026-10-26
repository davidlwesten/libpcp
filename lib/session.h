#pragma once

#include <pcp/pcp.h>

#include "epoch.h"
#include "mapping.h"

typedef struct pcp_session {
  pcp_config_t config;
  pcp_epoch_t epoch;
  pcp_mapping_t *mappings;
  pcp_handle_t handles; // last handle handed out
} pcp_session_t;
