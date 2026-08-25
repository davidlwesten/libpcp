#include <pcp/pcp.h>

#include "client.h"
#include "map.h"
#include "peer.h"
#include "server.h"
#include "transport.h"

#include <malloc.h>

static void pcp_client_run(pcp_client_t *client) {
  auto const internal = (pcp_client_internal_t *)client;
  CALL(internal->transport, run);
}

pcp_nonce_t pcp_client_create_nonce(pcp_client_internal_t * /*client*/) {
  return (pcp_nonce_t){.data = {}};
}

pcp_client_t *pcp_client_create(char const *pcp_server) {
  auto client = (pcp_client_internal_t *)malloc(sizeof(pcp_client_internal_t));
  if (!client) {
    return nullptr;
  }

  auto const transport = transport_connect(
      pcp_server, (transport_on_recv_t)pcp_server_recv, client);

  if (!transport.has_value) {
    free(client);
    return nullptr;
  }

  *client = (pcp_client_internal_t){.base = {.map = pcp_client_map_request,
                                             .peer = pcp_client_peer_request,
                                             .run = pcp_client_run},
                                    .server = {},
                                    .transport = transport.value};

  return (pcp_client_t *)client;
}

void pcp_client_destroy(pcp_client_t *client) {
  pcp_client_internal_t *internal = (pcp_client_internal_t *)client;
  CALL(internal->transport, close);
  free(internal);
}
