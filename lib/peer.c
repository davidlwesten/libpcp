#include <pcp/pcp.h>

#include "client.h"
#include "packet.h"
#include "peer.h"
#include "request.h"
#include "response.h"
#include "types.h"

static constexpr size_t PCP_PEER_REQUEST_BODY_SIZE = 56;

pcp_transport_const_view_t pcp_packet_write_peer_request(pcp_transport_view_t dst,
                                           pcp_peer_request_t const *src) {
  pcp_packet_write_request_header(dst, src->header);
  pcp_packet_body_write_bytes(
      dst, 0, (pcp_transport_const_view_t){sizeof(src->nonce.data), src->nonce.data});
  pcp_packet_body_write_uint8_t(dst, 12, src->protocol);
  pcp_packet_body_write_uint16_t(dst, 16, src->header.internal.port);
  pcp_packet_body_write_uint16_t(dst, 18, src->external.port);
  pcp_packet_body_write_ip_address(dst, 20, src->external.ip);
  pcp_packet_body_write_uint16_t(dst, 36, src->peer.port);
  pcp_packet_body_write_ip_address(dst, 40, src->peer.ip);

  return (pcp_transport_const_view_t){.size = PCP_HEADER_SIZE + PCP_PEER_REQUEST_BODY_SIZE,
                        .data = dst.data};
}

pcp_peer_request_t
pcp_packet_body_read_peer_request(pcp_request_header_t header,
                                  pcp_transport_const_view_t packet) {
  pcp_nonce_t nonce = {};
  pcp_packet_body_read_bytes(
      (pcp_transport_view_t){.size = sizeof(nonce.data), .data = nonce.data}, packet, 0);
  return (pcp_peer_request_t){
      .header = {.opcode = header.opcode,
                 .ttl = header.ttl,
                 .internal = {.ip = header.internal.ip,
                              .port =
                                  pcp_packet_body_read_uint16_t(packet, 16)}},
      .nonce = nonce,
      .protocol = pcp_packet_body_read_uint8_t(packet, 12),
      .external = {.ip = pcp_packet_body_read_ip_address(packet, 20),
                   .port = pcp_packet_body_read_uint16_t(packet, 18)},
      .peer = {.ip = pcp_packet_body_read_ip_address(packet, 40),
               .port = pcp_packet_body_read_uint16_t(packet, 36)}};
}

pcp_peer_response_t
pcp_packet_body_read_peer_response(pcp_response_header_t header,
                                   pcp_transport_const_view_t src) {
  pcp_nonce_t nonce = {0};
  pcp_packet_body_read_bytes((pcp_transport_view_t){sizeof(nonce.data), nonce.data}, src, 0);
  return (pcp_peer_response_t){
      .header = header,
      .nonce = nonce,
      .protocol = pcp_packet_body_read_uint8_t(src, 12),
      .internal = pcp_packet_body_read_uint16_t(src, 16),
      .external = {.ip = pcp_packet_body_read_ip_address(src, 20),
                   .port = pcp_packet_body_read_uint16_t(src, 18)},
      .peer = {.ip = pcp_packet_body_read_ip_address(src, 40),
               .port = pcp_packet_body_read_uint16_t(src, 36)}};
}

static void
pcp_client_peer_request_internal(pcp_client_internal_t *client,
                                 pcp_protocol_t protocol,
                                 port_t internal,
                                 socket_address_t external,
                                 socket_address_t peer,
                                 time_t ttl,
                                 size_t options_count,
                                 const pcp_option_t options[options_count]) {
  pcp_request_t const request = (pcp_request_t){
      .peer = {.header = {.opcode = PEER,
                          .ttl = ttl,
                          .internal = {.ip = CALL(&client->transport, local).ip,
                                       .port = internal}},
               .nonce = pcp_client_create_nonce(client),
               .protocol = protocol,
               .external = external,
               .peer = peer}};

  pcp_client_send(client, &request, options_count, options);
}

void pcp_client_peer_request(pcp_client_internal_t *client,
                             pcp_protocol_t protocol,
                             port_t internal_port,
                             socket_address_t external,
                             socket_address_t peer,
                             time_t ttl,
                             size_t options_count,
                             const pcp_option_t options[options_count]) {
  pcp_client_peer_request_internal(client, protocol,
                                   internal_port, external, peer, ttl,
                                   options_count, options);
}
