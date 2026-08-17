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

typedef struct pcp_peer_response {
  pcp_result_t code;
  time_t ttl;
  time_t epoch;
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  port_t internal;
  socket_address_t external;
  socket_address_t peer;
} pcp_peer_response_t;

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
  pcp_datagram_write_request_header(dst, PEER, src->ttl, src->internal.ip);
  pcp_datagram_body_write_bytes(dst, 0, sizeof(src->nonce.data),
                                src->nonce.data);
  pcp_datagram_body_write_uint8_t(dst, 12, (uint8_t)src->protocol);
  pcp_datagram_body_write_uint16_t(dst, 16, src->internal.port);
  pcp_datagram_body_write_uint16_t(dst, 18, src->external.port);
  pcp_datagram_body_write_ip_address(dst, 20, src->external.ip);
  pcp_datagram_body_write_uint16_t(dst, 36, src->peer.port);
  pcp_datagram_body_write_ip_address(dst, 40, src->peer.ip);
}

typedef struct pcp_peer_response_or_error {
  bool has_value;
  union {
    pcp_peer_response_t value;
    pcp_result_t error;
  };
} pcp_peer_response_or_error_t;

/*static pcp_peer_response_or_error_t
pcp_peer_response_create(const pcp_datagram_t *src) {
  pcp_opcode_t opcode = {0};
  pcp_result_t code = {0};
  time_t ttl = {0};
  time_t epoch = {0};
  pcp_datagram_read_response_header(&opcode, &code, &ttl, &epoch, src);
  if (opcode != PEER) {
    return (pcp_peer_response_or_error_t){.has_value = false,
                                          .error = UNSUPP_OPCODE};
  }

  pcp_nonce_t nonce = {0};
  pcp_datagram_body_read_bytes(sizeof(nonce.data), nonce.data, src, 0);

  pcp_protocol_t protocol = pcp_datagram_body_read_uint8_t(src, 12);
  if (!pcp_protocol_is_valid(protocol)) {
    return (pcp_peer_response_or_error_t){.has_value = false,
                                          .error = UNSUPP_PROTOCOL};
  }

  return (pcp_peer_response_or_error_t){
      .has_value = true,
      .value = {.code = code,
                .ttl = ttl,
                .epoch = epoch,
                .nonce = nonce,
                .protocol = protocol,
                .internal = pcp_datagram_body_read_uint16_t(src, 16),
                .external = {.ip = pcp_datagram_body_read_ip_address(src, 20),
                             .port = pcp_datagram_body_read_uint16_t(src, 18)},
                .peer = {.ip = pcp_datagram_body_read_ip_address(src, 40),
                         .port = pcp_datagram_body_read_uint16_t(src, 36)}}};
}*/

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
