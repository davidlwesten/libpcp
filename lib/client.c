#include <pcp/pcp.h>

#include "array.h"
#include "client.h"
#include "map.h"
#include "peer.h"
#include "server.h"
#include "transport.h"

#include <assert.h>
#include <malloc.h>

static void pcp_client_run(pcp_client_t *client) {
  auto const internal = (pcp_client_internal_t *)client;
  CALL(internal->transport, run);
}

pcp_nonce_t pcp_client_create_nonce(pcp_client_internal_t * /*client*/) {
  return (pcp_nonce_t){.data = {}};
}

static void pcp_client_on_send(pcp_client_internal_t *client,
                               packet_t *packet,
                               size_or_error_t result) {
  if (!result.has_value) {
    if (result.error == EAGAIN || result.error == EWOULDBLOCK ||
        result.error == EINTR || result.error == ENOBUFS) {
      CALL(client->transport, send,
           (const_view_t){.size = packet->size, .data = packet->data},
           (transport_on_send_t){
               .fn = (transport_on_send_callback_t)pcp_client_on_send,
               .userdata = packet});
      return;
    }
    packet_destroy(packet);
  }

  assert(result.value == packet->size);

  pcp_lease_t const new_lease = pcp_lease_create_pending(
      client->transport, packet, array_size(client->leases));
  array_push_back(client->leases, new_lease);
}

void pcp_client_send(pcp_client_internal_t *client,
                     const pcp_request_t *request) {
  auto const packet = pcp_packet_write_request(request);
  if (packet) {
    CALL(client->transport, send,
         (const_view_t){.size = packet->size, .data = packet->data},
         (transport_on_send_t){
             .fn = (transport_on_send_callback_t)pcp_client_on_send,
             .userdata = packet});
  }
}

pcp_client_t *pcp_client_create(char const *pcp_server) {
  auto client = (pcp_client_internal_t *)malloc(sizeof(pcp_client_internal_t));
  if (!client) {
    return nullptr;
  }

  auto const transport = transport_connect(
      pcp_server,
      (transport_on_recv_t){.fn = (transport_on_recv_callback_t)pcp_client_recv,
                            .userdata = nullptr},
      client);

  if (!transport.has_value) {
    free(client);
    return nullptr;
  }

  *client = (pcp_client_internal_t){.base = {.map = pcp_client_map_request,
                                             .peer = pcp_client_peer_request,
                                             .run = pcp_client_run},
                                    .server = {},
                                    .transport = transport.value,
                                    .leases = array_create(pcp_lease_t)};

  return (pcp_client_t *)client;
}

void pcp_client_destroy(pcp_client_t *client) {
  pcp_client_internal_t *internal = (pcp_client_internal_t *)client;
  CALL(internal->transport, close);
  free(internal);
}
