#include <pcp/pcp.h>

#include "datagram.h"
#include "internal.h"
#include "peer.h"

#include <assert.h>
#include <stdlib.h>

static constexpr size_t PCP_PEER_REQUEST_BODY_SIZE = 56;

typedef struct pcp_peer_request {
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  socket_address_t internal;
  socket_address_t external;
  socket_address_t peer;
  time_t ttl;
} pcp_peer_request_t;

static pcp_peer_request_t pcp_peer_request_create(pcp_protocol_t protocol,
                                                  socket_address_t internal,
                                                  socket_address_t external,
                                                  socket_address_t peer,
                                                  time_t ttl) {
  return (pcp_peer_request_t){.nonce = pcp_nonce_create(),
                              .protocol = protocol,
                              .internal = internal,
                              .external = external,
                              .peer = peer,
                              .ttl = ttl};
}

static void pcp_peer_request_to_datagram(pcp_datagram_t *dst,
                                         const pcp_peer_request_t *src) {
  pcp_datagram_write_request_header(
      (view_t){.size = sizeof(dst->data), .data = dst->data}, PEER, src->ttl,
      src->internal.ip);
  pcp_datagram_body_write_bytes(
      (view_t){sizeof(dst->data), dst->data}, 0,
      (const_view_t){sizeof(src->nonce.data), src->nonce.data});
  pcp_datagram_body_write_uint8_t(
      (view_t){.size = sizeof(dst->data), .data = dst->data}, 12,
      (uint8_t)src->protocol);
  pcp_datagram_body_write_uint16_t(
      (view_t){.size = sizeof(dst->data), .data = dst->data}, 16,
      src->internal.port);
  pcp_datagram_body_write_uint16_t(
      (view_t){.size = sizeof(dst->data), .data = dst->data}, 18,
      src->external.port);
  pcp_datagram_body_write_ip_address(
      (view_t){.size = sizeof(dst->data), .data = dst->data}, 20,
      src->external.ip);
  pcp_datagram_body_write_uint16_t(
      (view_t){.size = sizeof(dst->data), .data = dst->data}, 36,
      src->peer.port);
  pcp_datagram_body_write_ip_address(
      (view_t){.size = sizeof(dst->data), .data = dst->data}, 40, src->peer.ip);
}

pcp_peer_response_t
pcp_datagram_body_read_peer_response(const_view_t datagram) {
  pcp_nonce_t nonce = {0};
  pcp_datagram_body_read_bytes((view_t){sizeof(nonce.data), nonce.data},
                               datagram, 0);
  return (pcp_peer_response_t){
      .nonce = nonce,
      .protocol = pcp_datagram_body_read_uint8_t(datagram, 12),
      .internal = pcp_datagram_body_read_uint16_t(datagram, 16),
      .external = {.ip = pcp_datagram_body_read_ip_address(datagram, 20),
                   .port = pcp_datagram_body_read_uint16_t(datagram, 18)},
      .peer = {.ip = pcp_datagram_body_read_ip_address(datagram, 40),
               .port = pcp_datagram_body_read_uint16_t(datagram, 36)}};
}

static void pcp_client_internal_peer(const pcp_client_internal_t *client,
                                     pcp_protocol_t protocol,
                                     port_t internal_port,
                                     socket_address_t external,
                                     socket_address_t peer,
                                     time_t ttl,
                                     size_t options_count,
                                     const pcp_option_t[options_count],
                                     void (*)(pcp_mapping_or_error_t result)) {
  auto const request = pcp_peer_request_create(
      protocol,
      (socket_address_t){.ip = client->transport.local.ip,
                         .port = internal_port},
      external, peer, ttl);

  pcp_datagram_t datagram = {0};
  pcp_peer_request_to_datagram(&datagram, &request);

  constexpr size_t REQUEST_SIZE = PCP_HEADER_SIZE + PCP_PEER_REQUEST_BODY_SIZE;
  CALL(&client->transport, send, REQUEST_SIZE, datagram.data, nullptr);
  CALL(&client->transport, run);
}

void pcp_client_peer(const pcp_client_t *client,
                     pcp_protocol_t protocol,
                     port_t internal_port,
                     socket_address_t external,
                     socket_address_t peer,
                     time_t ttl,
                     size_t options_count,
                     const pcp_option_t options[options_count],
                     void (*callback)(pcp_mapping_or_error_t result)) {
  pcp_client_internal_peer((const pcp_client_internal_t *)client, protocol,
                           internal_port, external, peer, ttl, options_count,
                           options, callback);
}
