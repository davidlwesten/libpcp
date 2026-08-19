#include <pcp/pcp.h>

#include "option.h"
#include "packet.h"
#include "pcp/option.h"
#include "peer.h"

#include <assert.h>
#include <malloc.h>
#include <string.h>

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

static size_t
pcp_packet_write_peer_request(view_t dst,
                              const pcp_peer_request_t *src,
                              size_t options_count,
                              pcp_option_t const options[options_count]) {
  pcp_packet_write_request_header(dst, PEER, src->ttl, src->internal.ip);
  pcp_packet_body_write_bytes(
      dst, 0, (const_view_t){sizeof(src->nonce.data), src->nonce.data});
  pcp_packet_body_write_uint8_t(dst, 12, (uint8_t)src->protocol);
  pcp_packet_body_write_uint16_t(dst, 16, src->internal.port);
  pcp_packet_body_write_uint16_t(dst, 18, src->external.port);
  pcp_packet_body_write_ip_address(dst, 20, src->external.ip);
  pcp_packet_body_write_uint16_t(dst, 36, src->peer.port);
  pcp_packet_body_write_ip_address(dst, 40, src->peer.ip);

  size_t offset = PCP_PEER_REQUEST_BODY_SIZE;

  auto const begin = options;
  auto const end = begin + options_count;
  for (auto iter = begin; iter != end; ++iter) {
    switch (iter->code) {
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

  return PCP_HEADER_SIZE + offset;
}

pcp_peer_response_t pcp_packet_body_read_peer_response(const_view_t src) {
  pcp_nonce_t nonce = {0};
  pcp_packet_body_read_bytes((view_t){sizeof(nonce.data), nonce.data}, src, 0);
  return (pcp_peer_response_t){
      .nonce = nonce,
      .protocol = pcp_packet_body_read_uint8_t(src, 12),
      .internal = pcp_packet_body_read_uint16_t(src, 16),
      .external = {.ip = pcp_packet_body_read_ip_address(src, 20),
                   .port = pcp_packet_body_read_uint16_t(src, 18)},
      .peer = {.ip = pcp_packet_body_read_ip_address(src, 40),
               .port = pcp_packet_body_read_uint16_t(src, 36)}};
}

static void pcp_client_peer_request_on_send(struct transport const * /*self*/,
                                            size_or_error_t result,
                                            void * /*userptr*/) {
  assert(result.has_value);
}

static void
pcp_client_peer_request_internal(const pcp_client_internal_t *client,
                                 pcp_protocol_t protocol,
                                 port_t internal_port,
                                 socket_address_t external,
                                 socket_address_t peer,
                                 time_t ttl,
                                 size_t options_count,
                                 const pcp_option_t options[options_count],
                                 void (*)(pcp_mapping_or_error_t result)) {
  auto const request = pcp_peer_request_create(
      protocol,
      (socket_address_t){.ip = client->transport.local.ip,
                         .port = internal_port},
      external, peer, ttl);

  uint8_t packet[PCP_PACKET_MAX_SIZE];
  size_t const request_size = pcp_packet_write_peer_request(
      (view_t){.size = sizeof(packet), .data = packet}, &request, options_count,
      options);

  CALL(&client->transport, send,
       (const_view_t){.size = request_size, .data = packet},
       (transport_on_send_t){.fn = pcp_client_peer_request_on_send,
                             .userdata = nullptr});
  CALL(&client->transport, run);
}

void pcp_client_peer_request(const pcp_client_t *client,
                             pcp_protocol_t protocol,
                             port_t internal_port,
                             socket_address_t external,
                             socket_address_t peer,
                             time_t ttl,
                             size_t options_count,
                             const pcp_option_t options[options_count],
                             void (*callback)(pcp_mapping_or_error_t result)) {
  pcp_client_peer_request_internal((const pcp_client_internal_t *)client,
                                   protocol, internal_port, external, peer, ttl,
                                   options_count, options, callback);
}
