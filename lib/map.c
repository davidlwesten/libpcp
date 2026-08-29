#include <pcp/pcp.h>

#include "client.h"
#include "map.h"
#include "packet.h"
#include "request.h"
#include "response.h"
#include "types.h"

static constexpr size_t PCP_MAP_REQUEST_BODY_SIZE = 36;

const_view_t pcp_packet_write_map_request(view_t dst,
                                          pcp_map_request_t const *request) {
  pcp_packet_write_request_header(dst, request->header);
  pcp_packet_body_write_bytes(
      dst, 0, (const_view_t){sizeof(request->nonce.data), request->nonce.data});
  pcp_packet_body_write_uint8_t(dst, 12, request->protocol);
  pcp_packet_body_write_uint16_t(dst, 16, request->header.internal.port);
  pcp_packet_body_write_uint16_t(dst, 18, request->external.port);
  pcp_packet_body_write_ip_address(dst, 20, request->external.ip);

  return (const_view_t){.size = PCP_HEADER_SIZE + PCP_MAP_REQUEST_BODY_SIZE,
                        .data = dst.data};
}

pcp_map_request_t pcp_packet_body_read_map_request(pcp_request_header_t header,
                                                   const_view_t packet) {
  pcp_nonce_t nonce = {0};
  pcp_packet_body_read_bytes(
      (view_t){.size = sizeof(nonce.data), .data = nonce.data}, packet, 0);
  return (pcp_map_request_t){
      .header = {.opcode = header.opcode,
                 .ttl = header.ttl,
                 .internal = {.ip = header.internal.ip,
                              .port =
                                  pcp_packet_body_read_uint8_t(packet, 16)}},
      .nonce = nonce,
      .protocol = pcp_packet_body_read_uint8_t(packet, 12),
      .external = {.ip = pcp_packet_body_read_ip_address(packet, 20),
                   .port = pcp_packet_body_read_uint16_t(packet, 18)}};
}

pcp_map_response_t
pcp_packet_body_read_map_response(pcp_response_header_t header,
                                  const_view_t src) {
  pcp_nonce_t nonce = {0};
  pcp_packet_body_read_bytes((view_t){sizeof(nonce.data), nonce.data}, src, 0);
  return (pcp_map_response_t){
      .header = header,
      .nonce = nonce,
      .protocol = pcp_packet_body_read_uint8_t(src, 12),
      .internal = pcp_packet_body_read_uint16_t(src, 16),
      .external = {.ip = pcp_packet_body_read_ip_address(src, 20),
                   .port = pcp_packet_body_read_uint16_t(src, 18)}};
}

static void
pcp_client_map_request_internal(pcp_client_internal_t *client,
                                pcp_protocol_t protocol,
                                port_t internal,
                                socket_address_t external,
                                time_t ttl,
                                size_t options_count,
                                pcp_option_t const options[options_count]) {
  pcp_request_t const request = {
      .map = {.header = {.opcode = MAP,
                         .ttl = ttl,
                         .internal = {.ip = client->transport->local.ip,
                                      .port = internal}},
              .nonce = pcp_client_create_nonce(client),
              .protocol = protocol,
              .external = external}};

  pcp_client_send(client, &request, options_count, options);
}

void pcp_client_map_request(pcp_client_t *client,
                            pcp_protocol_t protocol,
                            port_t internal_port,
                            socket_address_t external,
                            time_t ttl,
                            size_t options_count,
                            pcp_option_t const options[options_count]) {
  pcp_client_map_request_internal((pcp_client_internal_t *)client, protocol,
                                  internal_port, external, ttl, options_count,
                                  options);
}
