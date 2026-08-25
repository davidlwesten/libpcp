#pragma once

#include <pcp/ip.h>
#include <pcp/result.h>
#include <stdint.h>

#include "request.h"
#include "response.h"
#include "types.h"

constexpr size_t PCP_PACKET_MAX_SIZE = 1100;
constexpr size_t PCP_HEADER_SIZE = 24;

typedef struct packet {
  size_t size;
  uint8_t data[];
} packet_t;

packet_t *packet_create(size_t size, uint8_t const src[size]);
void packet_destroy(packet_t *packet);

packet_t *pcp_packet_write_request(pcp_request_t const *src);
void pcp_packet_write_request_header(view_t dst, pcp_request_header_t header);
void pcp_packet_body_write_bytes(view_t dst, size_t offset, const_view_t bytes);
void pcp_packet_body_write_uint8_t(view_t dst, size_t offset, uint8_t value);
void pcp_packet_body_write_uint16_t(view_t dst, size_t offset, uint16_t value);
void pcp_packet_body_write_ip_address(view_t dst,
                                      size_t offset,
                                      ip_address_t ip);

pcp_request_t pcp_packet_read_request(const_view_t src);
pcp_response_header_t pcp_packet_read_response_header(const_view_t src);
pcp_response_t pcp_packet_read_response(const_view_t src);
void pcp_packet_body_read_bytes(view_t dst, const_view_t src, size_t offset);
uint8_t pcp_packet_body_read_uint8_t(const_view_t src, size_t offset);
uint16_t pcp_packet_body_read_uint16_t(const_view_t src, size_t offset);
ip_address_t pcp_packet_body_read_ip_address(const_view_t src, size_t offset);
