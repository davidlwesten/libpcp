#pragma once

#include <pcp/pcp.h>

#include "types.h"

typedef struct pcp_epoch {
  bool has_value;
  struct {
    time_t server_time;
    time_t client_time;
  } value;
} pcp_epoch_t;

typedef struct pcp_server {
  pcp_epoch_t epoch;
} pcp_server_t;

typedef struct pcp_client_internal pcp_client_internal_t;

void pcp_client_recv(pcp_client_internal_t *client,
                     void *userdata,
                     const_view_t packet);
