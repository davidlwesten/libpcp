#include <pcp/pcp.h>

#include "client.h"
#include "map.h"
#include "packet.h"
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
                               packet_t *request,
                               size_or_error_t result) {
  if (!result.has_value) {
    if (result.error == EAGAIN || result.error == EWOULDBLOCK ||
        result.error == EINTR || result.error == ENOBUFS) {
      CALL(client->transport, send,
           (const_view_t){.size = request->size, .data = request->data},
           (transport_on_send_t){
               .fn = (transport_on_send_callback_t)pcp_client_on_send,
               .userdata = request});
      return;
    }
    packet_destroy(request);
  }

  assert(result.value == request->size);

  auto const begin = client->leases;
  auto const end = begin + sizeof(client->leases);
  auto iter = begin;
  for (; iter != end; ++iter) {
    if (!iter->has_value) {
      break;
    }
  }

  assert(iter != end);
  iter->has_value = true;
  pcp_pending_state_create(&iter->value, client->transport, request);
}

void pcp_client_send(pcp_client_internal_t *client,
                     const pcp_request_t *request,
                     size_t options_count,
                     pcp_option_t const options[options_count]) {
  uint8_t buffer[PCP_PACKET_MAX_SIZE] = {0};
  auto const view =
      pcp_packet_write_request((view_t){.size = sizeof(buffer), .data = buffer},
                               request, options_count, options);

  auto const packet = packet_create(view.size, view.data);
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
                                    .leases = {}};

  return (pcp_client_t *)client;
}

void pcp_client_destroy(pcp_client_t *client) {
  pcp_client_internal_t *internal = (pcp_client_internal_t *)client;
  CALL(internal->transport, close);
  free(internal);
}
