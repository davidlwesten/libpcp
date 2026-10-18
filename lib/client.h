#pragma once

#include <pcp/pcp.h>

#include "lease.h"
#include "server.h"

constexpr size_t PCP_CLIENT_MAX_LEASES = 10;

typedef struct optional_lease {
  bool has_value;
  pcp_lease_t value;
} optional_lease_t;

typedef struct pcp_client_internal pcp_client_internal_t;

typedef struct pcp_client_internal {
  pcp_server_t server;
  pcp_transport_t transport;
  pcp_allocator_t allocator;
  optional_lease_t leases[PCP_CLIENT_MAX_LEASES];
  time_t (*time)();

  pcp_optional_error_t shutdown_error;
  pcp_client_on_shutdown_t shutdown_callback;
  void *shutdown_userdata;
} pcp_client_internal_t;

void pcp_client_on_lease_shutdown_complete(void *userdata,
                                           pcp_optional_error_t result);

pcp_nonce_t pcp_client_create_nonce(pcp_client_internal_t *client);
void pcp_client_send(pcp_client_internal_t *client,
                     pcp_request_t const *request,
                     size_t options_count,
                     pcp_option_t const[options_count]);
