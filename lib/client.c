#include <pcp/pcp.h>

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

static void pcp_client_on_send(void *packet, size_or_error_t result) {
  assert(result.has_value);
  free(packet);
}

void pcp_client_send(pcp_client_internal_t const *client,
                     const pcp_request_t *request) {
  packet_t packet = {0};
  switch (request->header.opcode) {
  case MAP:
    packet = pcp_packet_write_map_request(&request->map);
    break;
  case PEER:
    packet = pcp_packet_write_peer_request(&request->peer);
    break;
  case ANNOUNCE:
    assert(false);
    break;
  }

  CALL(
      client->transport, send,
      (const_view_t){.size = packet.size, .data = packet.data},
      (transport_on_send_t){.fn = pcp_client_on_send, .userdata = packet.data});
}

pcp_client_t *pcp_client_create(char const *pcp_server) {
  auto client = (pcp_client_internal_t *)malloc(sizeof(pcp_client_internal_t));
  if (!client) {
    return nullptr;
  }

  auto const transport = transport_connect(
      pcp_server,
      (transport_on_recv_t){.fn = (transport_on_recv_callback_t)pcp_client_recv,
                            .userdata = client});

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
