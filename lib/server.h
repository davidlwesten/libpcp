#pragma once

#include <pcp/pcp.h>

#include "transport.h"

typedef struct pcp_epoch {
  time_t server;
  time_t client;
} pcp_epoch_t;

typedef struct pcp_server {
  pcp_epoch_t epoch;
} pcp_server_t;

typedef struct pcp_client_internal pcp_client_internal_t;

void pcp_server_recv(transport_t const *self,
                     pcp_client_internal_t *client,
                     const_view_t packet);
