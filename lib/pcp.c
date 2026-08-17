#include <pcp/pcp.h>

#include "internal.h"
#include "map.h"
#include "peer.h"
#include "transport.h"

#include <assert.h>
#include <malloc.h>

pcp_nonce_t pcp_nonce_create() { return (pcp_nonce_t){.data = {0}}; }

bool pcp_result_is_valid(pcp_result_t code) {
  switch (code) {
  case SUCCESS:
  case UNSUPP_VERSION:
  case NOT_AUTHORIZED:
  case MALFORMED_REQUEST:
  case UNSUPP_OPCODE:
  case UNSUPP_OPTION:
  case MALFORMED_OPTION:
  case NETWORK_FAILURE:
  case NO_RESOURCES:
  case UNSUPP_PROTOCOL:
  case USER_EX_QUOTA:
  case CANNOT_PROVIDE_EXTERNAL:
  case ADDRESS_MISMATCH:
  case EXCESSIVE_REMOTE_PEERS:
    break;
  default:
    return false;
  }
  return true;
}

bool pcp_protocol_is_valid(pcp_protocol_t protocol) {
  switch (protocol) {
  case ALL:
  case UDP:
  case TCP:
    break;
  default:
    return false;
  }

  return true;
}

static void pcp_server_recv(transport_t const *self,
                            pcp_server_t * /*ctx*/,
                            size_t size,
                            const uint8_t /*data*/[size]) {
  CALL(self, close);
}

pcp_client_t *pcp_client_create(const char *pcp_server) {
  auto client = (pcp_client_internal_t *)malloc(sizeof(pcp_client_internal_t));
  if (!client) {
    return nullptr;
  }

  auto const transport = transport_connect(
      pcp_server, (on_data_t){.callback = (on_data_callback_t)pcp_server_recv,
                              .ctx = &client->server});
  if (!transport.has_value) {
    free(client);
    return nullptr;
  }

  *(pcp_client_internal_t *)client =
      (pcp_client_internal_t){.base = {.local = transport.value.local,
                                       .remote = transport.value.remote,
                                       .map = pcp_client_map,
                                       .peer = pcp_client_peer},
                              .transport = transport.value};

  return (pcp_client_t *)client;
}

void pcp_client_destroy(pcp_client_t *client) {
  pcp_client_internal_t *internal = (pcp_client_internal_t *)client;
  CALL(&internal->transport, close);
  free(internal);
}
