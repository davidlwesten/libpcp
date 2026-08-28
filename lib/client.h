#pragma once

#include <pcp/pcp.h>

#include "lease.h"
#include "server.h"
#include "transport.h"

constexpr size_t PCP_CLIENT_MAX_LEASES = 10;

typedef struct optional_lease {
  bool has_value;
  pcp_lease_t value;
} optional_lease_t;

typedef struct pcp_client_internal {
  pcp_client_t base;
  pcp_server_t server;
  transport_t *transport;
  optional_lease_t leases[PCP_CLIENT_MAX_LEASES];
} pcp_client_internal_t;

pcp_nonce_t pcp_client_create_nonce(pcp_client_internal_t *client);
void pcp_client_send(pcp_client_internal_t *client,
                     pcp_request_t const *request);
