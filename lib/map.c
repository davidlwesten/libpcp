#include <pcp/pcp.h>

#include "client.h"
#include "datagram.h"
#include "map.h"
#include "types.h"

#include <assert.h>

static constexpr size_t PCP_MAP_REQUEST_BODY_SIZE = 36;

typedef struct pcp_map_request {
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  socket_address_t internal;
  socket_address_t external;
  time_t ttl;
} pcp_map_request_t;

static pcp_map_request_t pcp_map_request_create(pcp_protocol_t protocol,
                                                socket_address_t internal,
                                                socket_address_t external,
                                                time_t ttl) {
  assert(protocol == ALL || protocol == TCP || protocol == UDP);
  return (pcp_map_request_t){.nonce = pcp_nonce_create(),
                             .protocol = protocol,
                             .internal = internal,
                             .external = external,
                             .ttl = ttl};
}

static void pcp_map_request_to_datagram(view_t dst,
                                        const pcp_map_request_t *src) {
  pcp_datagram_write_request_header(dst, MAP, src->ttl, src->internal.ip);
  pcp_datagram_body_write_bytes(
      dst, 0, (const_view_t){sizeof(src->nonce.data), src->nonce.data});
  pcp_datagram_body_write_uint8_t(dst, 12, (uint8_t)src->protocol);
  pcp_datagram_body_write_uint16_t(dst, 16, src->internal.port);
  pcp_datagram_body_write_uint16_t(dst, 18, src->external.port);
  pcp_datagram_body_write_ip_address(dst, 20, src->external.ip);
}

pcp_map_response_t pcp_datagram_body_read_map_response(const_view_t datagram) {
  pcp_nonce_t nonce = {0};
  pcp_datagram_body_read_bytes((view_t){sizeof(nonce.data), nonce.data},
                               datagram, 0);
  return (pcp_map_response_t){
      .nonce = nonce,
      .protocol = pcp_datagram_body_read_uint8_t(datagram, 12),
      .internal = pcp_datagram_body_read_uint16_t(datagram, 16),
      .external = {.ip = pcp_datagram_body_read_ip_address(datagram, 20),
                   .port = pcp_datagram_body_read_uint16_t(datagram, 18)}};
}

static void
pcp_client_map_request_internal(const pcp_client_internal_t *client,
                                pcp_protocol_t protocol,
                                port_t internal_port,
                                socket_address_t external,
                                time_t ttl,
                                size_t options_count,
                                const pcp_option_t[options_count],
                                void (*)(pcp_mapping_or_error_t result)) {

  auto const request =
      pcp_map_request_create(protocol,
                             (socket_address_t){
                                 .ip = client->transport.local.ip,
                                 .port = internal_port,
                             },
                             external, ttl);

  uint8_t packet[PCP_DATAGRAM_MAX_SIZE];
  pcp_map_request_to_datagram((view_t){.size = sizeof(packet), .data = packet},
                              &request);

  constexpr size_t REQUEST_SIZE = PCP_HEADER_SIZE + PCP_MAP_REQUEST_BODY_SIZE;
  CALL(&client->transport, send,
       (const_view_t){.size = REQUEST_SIZE, .data = packet},
       (transport_on_send_t){});
  CALL(&client->transport, run);
}

void pcp_client_map_request(const pcp_client_t *client,
                            pcp_protocol_t protocol,
                            port_t internal_port,
                            socket_address_t external,
                            time_t ttl,
                            size_t options_count,
                            const pcp_option_t options[options_count],
                            void (*callback)(pcp_mapping_or_error_t)) {
  pcp_client_map_request_internal((const pcp_client_internal_t *)client,
                                  protocol, internal_port, external, ttl,
                                  options_count, options, callback);
}
