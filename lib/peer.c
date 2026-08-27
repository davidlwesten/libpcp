#include <pcp/pcp.h>

#include "client.h"
#include "option.h"
#include "packet.h"
#include "peer.h"
#include "request.h"
#include "response.h"
#include "transport.h"

static constexpr size_t PCP_PEER_REQUEST_BODY_SIZE = 56;

packet_t *pcp_packet_write_peer_request(pcp_peer_request_t const *src) {
  uint8_t buffer[PCP_PACKET_MAX_SIZE] = {0};
  view_t dst = {.size = sizeof(buffer), .data = buffer};

  pcp_packet_write_request_header(dst, src->header);
  pcp_packet_body_write_bytes(
      dst, 0, (const_view_t){sizeof(src->nonce.data), src->nonce.data});
  pcp_packet_body_write_uint8_t(dst, 12, src->protocol);
  pcp_packet_body_write_uint16_t(dst, 16, src->header.internal.port);
  pcp_packet_body_write_uint16_t(dst, 18, src->external.port);
  pcp_packet_body_write_ip_address(dst, 20, src->external.ip);
  pcp_packet_body_write_uint16_t(dst, 36, src->peer.port);
  pcp_packet_body_write_ip_address(dst, 40, src->peer.ip);

  size_t offset = PCP_PEER_REQUEST_BODY_SIZE;

  auto const begin = src->options;
  auto const end = begin + src->options_count;
  for (auto iter = begin; iter != end; ++iter) {
    switch ((iter->code) & 0x7F) {
    case THIRD_PARTY:
      pcp_packet_body_write_third_party(dst, offset, &iter->third_party);
      offset += PCP_THIRD_PARTY_OPTION_SIZE;
      break;
    case PREFER_FAILURE:
      pcp_packet_body_write_prefer_failure(dst, offset, &iter->prefer_failure);
      offset += PCP_PREFER_FAILURE_OPTION_SIZE;
      break;
    case FILTER:
      pcp_packet_body_write_filter(dst, offset, &iter->filter);
      offset += PCP_FILTER_OPTION_SIZE;
      break;
    }
  }

  return packet_create(PCP_HEADER_SIZE + offset, buffer);
}

pcp_peer_request_t
pcp_packet_body_read_peer_request(pcp_request_header_t header,
                                  const_view_t packet) {
  pcp_nonce_t nonce = {};
  pcp_packet_body_read_bytes(
      (view_t){.size = sizeof(nonce.data), .data = nonce.data}, packet, 0);
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
               .port = pcp_packet_body_read_uint16_t(packet, 36)},
      .options_count = 0,
      .options = nullptr};
}

pcp_peer_response_t
pcp_packet_body_read_peer_response(pcp_response_header_t header,
                                   const_view_t src) {
  pcp_nonce_t nonce = {0};
  pcp_packet_body_read_bytes((view_t){sizeof(nonce.data), nonce.data}, src, 0);
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
                          .internal = {.ip = client->transport->local.ip,
                                       .port = internal}},
               .nonce = pcp_client_create_nonce(client),
               .protocol = protocol,
               .external = external,
               .peer = peer,
               .options_count = options_count,
               .options = options}};

  pcp_client_send(client, &request);
}

void pcp_client_peer_request(pcp_client_t *client,
                             pcp_protocol_t protocol,
                             port_t internal_port,
                             socket_address_t external,
                             socket_address_t peer,
                             time_t ttl,
                             size_t options_count,
                             const pcp_option_t options[options_count]) {
  pcp_client_peer_request_internal((pcp_client_internal_t *)client, protocol,
                                   internal_port, external, peer, ttl,
                                   options_count, options);
}
