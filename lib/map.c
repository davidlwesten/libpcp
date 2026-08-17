#include "map.h"
#include "datagram.h"
#include "internal.h"
#include "pcp/pcp.h"
#include "socket.h"

#include <assert.h>

static constexpr size_t PCP_MAP_REQUEST_BODY_SIZE = 36;

typedef struct pcp_map_request {
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  socket_address_t internal;
  socket_address_t external;
  time_t ttl;
} pcp_map_request_t;

typedef struct pcp_map_response {
  pcp_result_t code;
  time_t ttl;
  time_t epoch;
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  port_t internal;
  socket_address_t external;
} pcp_map_response_t;

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

static void pcp_map_request_to_datagram(pcp_datagram_t *dst,
                                        const pcp_map_request_t *src) {
  pcp_datagram_write_request_header(dst, MAP, src->ttl, src->internal.ip);
  pcp_datagram_body_write_bytes(dst, 0, sizeof(src->nonce.data),
                                src->nonce.data);
  pcp_datagram_body_write_uint8_t(dst, 12, (uint8_t)src->protocol);
  pcp_datagram_body_write_uint16_t(dst, 16, src->internal.port);
  pcp_datagram_body_write_uint16_t(dst, 18, src->external.port);
  pcp_datagram_body_write_ip_address(dst, 20, src->external.ip);
}

typedef struct pcp_map_response_or_error {
  bool has_value;
  union {
    pcp_map_response_t value;
    pcp_result_t error;
  };
} pcp_map_response_or_error_t;

/*static pcp_map_response_or_error_t
pcp_map_response_create(const pcp_datagram_t *src) {
  pcp_opcode_t opcode = {0};
  pcp_result_t code = {0};
  time_t ttl = {0};
  time_t epoch = {0};
  pcp_datagram_read_response_header(&opcode, &code, &ttl, &epoch, src);
  if (opcode != MAP) {
    return (pcp_map_response_or_error_t){.has_value = false,
                                         .error = UNSUPP_OPCODE};
  }

  if (!pcp_result_is_valid(code)) {
    return (pcp_map_response_or_error_t){.has_value = false,
                                         .error = MALFORMED_REQUEST};
  }

  pcp_nonce_t nonce = {0};
  pcp_datagram_body_read_bytes(sizeof(nonce.data), nonce.data, src, 0);

  const pcp_protocol_t protocol = pcp_datagram_body_read_uint8_t(src, 12);
  if (!pcp_protocol_is_valid(protocol)) {
    return (pcp_map_response_or_error_t){.has_value = false,
                                         .error = UNSUPP_PROTOCOL};
  }

  return (pcp_map_response_or_error_t){
      .has_value = true,
      .value = {
          .code = code,
          .ttl = ttl,
          .epoch = epoch,
          .nonce = nonce,
          .protocol = protocol,
          .internal = pcp_datagram_body_read_uint16_t(src, 16),
          .external = {.ip = pcp_datagram_body_read_ip_address(src, 20),
                       .port = pcp_datagram_body_read_uint16_t(src, 18)}}};
}*/

static void pcp_client_internal_map(const pcp_client_internal_t *client,
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

  pcp_datagram_t datagram = {0};
  pcp_map_request_to_datagram(&datagram, &request);

  constexpr size_t REQUEST_SIZE = PCP_HEADER_SIZE + PCP_MAP_REQUEST_BODY_SIZE;
  CALL(&client->transport, send, REQUEST_SIZE, datagram.data, nullptr);
  CALL(&client->transport, run);
}

void pcp_client_map(const pcp_client_t *client,
                    pcp_protocol_t protocol,
                    port_t internal_port,
                    socket_address_t external,
                    time_t ttl,
                    size_t options_count,
                    const pcp_option_t options[options_count],
                    void (*callback)(pcp_mapping_or_error_t)) {
  pcp_client_internal_map((const pcp_client_internal_t *)client, protocol,
                          internal_port, external, ttl, options_count, options,
                          callback);
}
