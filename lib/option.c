#include "option.h"
#include "datagram.h"

void pcp_datagram_body_write_third_party(pcp_datagram_t *dst,
                                         size_t offset,
                                         const pcp_option_third_party_t *src) {
  pcp_datagram_body_write_uint8_t(dst, offset, THIRD_PARTY);
  pcp_datagram_body_write_uint16_t(dst, offset + 2, 16);
  pcp_datagram_body_write_ip_address(dst, offset + 4, src->internal);
}

void pcp_datagram_body_write_prefer_failure(
    pcp_datagram_t *dst,
    size_t offset,
    const pcp_option_prefer_failure_t * /*src*/) {
  pcp_datagram_body_write_uint8_t(dst, offset, PREFER_FAILURE);
}

void pcp_datagram_body_write_filter(pcp_datagram_t *dst,
                                    size_t offset,
                                    const pcp_option_filter_t *src) {
  pcp_datagram_body_write_uint8_t(dst, offset, FILTER);
  pcp_datagram_body_write_uint16_t(dst, offset + 2, 20);
  pcp_datagram_body_write_uint8_t(dst, offset + 5, src->prefix);
  pcp_datagram_body_write_uint16_t(dst, offset + 6, src->peer.port);
  pcp_datagram_body_write_ip_address(dst, offset + 8, src->peer.ip);
}
