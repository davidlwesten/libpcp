#include "option.h"

#include "packet.h"

void pcp_packet_body_write_third_party(view_t dst,
                                       size_t offset,
                                       const pcp_option_third_party_t *src) {
  pcp_packet_body_write_uint8_t(dst, offset, src->code);
  pcp_packet_body_write_uint16_t(dst, offset + 2, 16);
  pcp_packet_body_write_ip_address(dst, offset + 4, src->internal);
}

void pcp_packet_body_write_prefer_failure(
    view_t dst,
    size_t offset,
    const pcp_option_prefer_failure_t *src) {
  pcp_packet_body_write_uint8_t(dst, offset, src->code);
}

void pcp_packet_body_write_filter(view_t dst,
                                  size_t offset,
                                  const pcp_option_filter_t *src) {
  pcp_packet_body_write_uint8_t(dst, offset, src->code);
  pcp_packet_body_write_uint16_t(dst, offset + 2, 20);
  pcp_packet_body_write_uint8_t(dst, offset + 5, src->prefix);
  pcp_packet_body_write_uint16_t(dst, offset + 6, src->peer.port);
  pcp_packet_body_write_ip_address(dst, offset + 8, src->peer.ip);
}

pcp_option_t pcp_option_create_third_party(bool required,
                                           ip_address_t internal) {
  return (pcp_option_t){
      .third_party = {.code = THIRD_PARTY | (required ? REQUIRED : 0),
                      .internal = internal}};
}

pcp_option_t pcp_option_create_prefer_failure(bool required) {
  return (pcp_option_t){
      .prefer_failure = {.code = PREFER_FAILURE | (required ? REQUIRED : 0)}};
}

pcp_option_t
pcp_option_create_filter(bool required, socket_address_t peer, uint8_t prefix) {
  return (pcp_option_t){.filter = {.code = FILTER | (required ? REQUIRED : 0),
                                   .peer = peer,
                                   .prefix = prefix}};
}
