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

#define MIN(x, y) ((x) < (y)) ? (x) : (y)
#define MAX(x, y) ((x) > (y)) ? (x) : (y)
#define CLAMP(X, MIN_, MAX_) MIN(MAX(MIN_, X), X)

static bool pcp_mapping_is_equal(pcp_mapping_t const *lhs,
                                 pcp_mapping_t const *rhs) {
  const int external_cmp = socket_address_cmp(&lhs->external, &rhs->external);
  const int internal_cmp = socket_address_cmp(&lhs->internal, &rhs->internal);
  const int protocol_cmp =
      CLAMP((int32_t)rhs->protocol - (int32_t)lhs->protocol, -1, 1);
  return (external_cmp + internal_cmp + protocol_cmp) == 0;
}

static void pcp_server_add_or_update_mapping(pcp_server_t *server,
                                             pcp_mapping_t mapping) {
  pcp_mapping_t *target = nullptr;
  for (size_t i = 0; i < server->active.size; ++i) {
    if (pcp_mapping_is_equal(server->active.data + i, &mapping)) {
      target = server->active.data + i;
      break;
    }
  }

  if (!target) {
    server->active.size += 1;
    if (server->active.capacity < server->active.size) {
      server->active.capacity = MAX(1, server->active.capacity * 2);
      size_t const alloc_size = server->active.capacity * sizeof(pcp_mapping_t);
      server->active.data =
          (pcp_mapping_t *)realloc(server->active.data, alloc_size);
      assert(server->active.data != nullptr);
    }
    target = server->active.data + (server->active.size - 1);
  }

  *target = mapping;
}

static void pcp_server_recv_announce(transport_t const * /*transport*/,
                                     pcp_server_t * /*server*/,
                                     const_view_t /*datagram*/) {}

static void pcp_server_recv_map(transport_t const *transport,
                                pcp_server_t *server,
                                pcp_result_t code,
                                time_t ttl,
                                const_view_t datagram) {
  pcp_map_response_t const response =
      pcp_datagram_body_read_map_response(datagram);
  if (code == SUCCESS) {
    pcp_server_add_or_update_mapping(
        server, (pcp_mapping_t){.protocol = response.protocol,
                                .internal = {.ip = transport->local.ip,
                                             .port = response.internal},
                                .external = response.external,
                                .ttl = ttl});
  }
}

static void pcp_server_recv_peer(transport_t const *transport,
                                 pcp_server_t *server,
                                 pcp_result_t code,
                                 time_t ttl,
                                 const_view_t datagram) {
  pcp_peer_response_t const response =
      pcp_datagram_body_read_peer_response(datagram);
  if (code == SUCCESS) {
    pcp_server_add_or_update_mapping(
        server, (pcp_mapping_t){.protocol = response.protocol,
                                .internal = {.ip = transport->local.ip,
                                             .port = response.internal},
                                .external = response.external,
                                .ttl = ttl});
  }
}

static void pcp_server_recv(transport_t const *self,
                            pcp_server_t *server,
                            const_view_t datagram) {
  pcp_opcode_t opcode;
  pcp_result_t code;
  time_t ttl;
  time_t epoch;
  pcp_datagram_read_response_header(&opcode, &code, &ttl, &epoch, datagram);

  switch (opcode) {
  case ANNOUNCE:
    pcp_server_recv_announce(self, server, datagram);
    break;
  case MAP:
    pcp_server_recv_map(self, server, code, ttl, datagram);
    break;
  case PEER:
    pcp_server_recv_peer(self, server, code, ttl, datagram);
    break;
  default:
    assert(false);
    break;
  }

  CALL(self, close);
}

pcp_client_t *pcp_client_create(char const *pcp_server) {
  auto client = (pcp_client_internal_t *)malloc(sizeof(pcp_client_internal_t));
  if (!client) {
    return nullptr;
  }

  auto const transport = transport_connect(
      pcp_server, (on_transport_data_t){
                      .callback = (on_transport_data_callback_t)pcp_server_recv,
                      .ctx = &client->server});
  if (!transport.has_value) {
    free(client);
    return nullptr;
  }

  *(pcp_client_internal_t *)client = (pcp_client_internal_t){
      .base = {.local = transport.value.local,
               .remote = transport.value.remote,
               .map = pcp_client_map,
               .peer = pcp_client_peer},
      .server = {.active = {.capacity = 0, .size = 0, .data = nullptr}},
      .transport = transport.value};

  return (pcp_client_t *)client;
}

void pcp_client_destroy(pcp_client_t *client) {
  pcp_client_internal_t *internal = (pcp_client_internal_t *)client;
  CALL(&internal->transport, close);
  free(internal);
}
