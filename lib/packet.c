#include "packet.h"

#include "map.h"
#include "peer.h"
#include "request.h"

#include <assert.h>
#include <malloc.h>
#include <string.h>

static constexpr uint8_t PCP_VERSION = 2;

static void
pcp_packet_write_uint16_t(view_t dst, size_t offset, uint16_t value) {
  assert(dst.size >= (offset + 2));
  dst.data[offset + 0] = (uint8_t)(value >> 8);
  dst.data[offset + 1] = (uint8_t)(value & 0xFF);
}

static void
pcp_packet_write_uint32_t(view_t dst, size_t offset, uint32_t value) {
  assert(dst.size >= (offset + 4));
  dst.data[offset + 0] = (uint8_t)(value >> 24);
  dst.data[offset + 1] = (uint8_t)(value >> 16);
  dst.data[offset + 2] = (uint8_t)(value >> 8);
  dst.data[offset + 3] = (uint8_t)(value & 0xFF);
}

static void
pcp_packet_write_ip_address(view_t dst, size_t offset, const ip_address_t ip) {
  assert(dst.size >= (offset + sizeof(ip_address_t)));
  memcpy(dst.data + offset, ip.addr, sizeof(ip.addr));
}

static uint16_t pcp_packet_read_uint16_t(const_view_t src, size_t offset) {
  assert(src.size >= (offset + 2));
  return (uint16_t)(src.data[offset + 0] << 8) | (uint16_t)src.data[offset + 1];
}

static uint32_t pcp_packet_read_uint32_t(const_view_t src, size_t offset) {
  assert(src.size >= (offset + 4));
  return ((uint32_t)src.data[offset + 0] << 24) |
         ((uint32_t)src.data[offset + 1] << 16) |
         ((uint32_t)src.data[offset + 2] << 8) |
         ((uint32_t)src.data[offset + 3]);
}

static ip_address_t pcp_packet_read_ip_address(const_view_t src,
                                               size_t offset) {
  assert(src.size >= (offset + sizeof(ip_address_t)));
  ip_address_t result = {0};
  memcpy(result.addr, src.data + offset, sizeof(result.addr));
  return result;
}

static pcp_request_header_t pcp_packet_read_request_header(const_view_t src) {
  assert(src.size > 0 && src.data[0] == PCP_VERSION);
  assert(src.size > 1 && !(src.data[1] & 0x80));
  return (pcp_request_header_t){
      .opcode = src.data[1],
      .ttl = pcp_packet_read_uint32_t(src, 4),
      .internal = {.ip = pcp_packet_read_ip_address(src, 8), .port = 0}};
}

void pcp_packet_write_request_header(view_t dst, pcp_request_header_t header) {
  assert(header.opcode == ANNOUNCE || header.opcode == MAP ||
         header.opcode == PEER);
  dst.data[0] = PCP_VERSION;
  dst.data[1] = header.opcode;
  pcp_packet_write_uint32_t(dst, 4, (uint32_t)header.ttl);
  pcp_packet_write_ip_address(dst, 8, header.internal.ip);
}

packet_t pcp_packet_write_request(const pcp_request_t *src) {
  switch (src->header.opcode) {
  case MAP:
    return pcp_packet_write_map_request(&src->map);
  case PEER:
    return pcp_packet_write_peer_request(&src->peer);
  default:
    assert(false);
    return (packet_t){.size = 0, .data = nullptr};
  }
}

void pcp_packet_body_write_bytes(view_t dst, size_t offset, const_view_t src) {
  assert(dst.size >= (PCP_HEADER_SIZE + offset + src.size));
  memcpy(dst.data + PCP_HEADER_SIZE + offset, src.data, src.size);
}

void pcp_packet_body_write_uint8_t(view_t dst, size_t offset, uint8_t value) {
  assert(dst.size >= (PCP_HEADER_SIZE + offset));
  dst.data[PCP_HEADER_SIZE + offset] = value;
}

void pcp_packet_body_write_uint16_t(view_t dst, size_t offset, uint16_t value) {
  pcp_packet_write_uint16_t(dst, PCP_HEADER_SIZE + offset, value);
}

void pcp_packet_body_write_ip_address(view_t dst,
                                      size_t offset,
                                      ip_address_t ip) {
  pcp_packet_write_ip_address(dst, PCP_HEADER_SIZE + offset, ip);
}

pcp_request_t pcp_packet_read_request(const_view_t src) {
  auto const header = pcp_packet_read_request_header(src);
  switch (header.opcode) {
  case MAP:
    return (pcp_request_t){.map =
                               pcp_packet_body_read_map_request(header, src)};
  case PEER:
    return (pcp_request_t){.peer =
                               pcp_packet_body_read_peer_request(header, src)};
  case ANNOUNCE:
    return (pcp_request_t){.announce.header = header};
  }
  unreachable();
}

pcp_response_header_t pcp_packet_read_response_header(const_view_t src) {
  assert(src.size > 0 && src.data[0] == PCP_VERSION);
  assert(src.size > 1 && (src.data[1] & 0x80));
  return (pcp_response_header_t){.opcode = (src.data[1] & 0x7F),
                                 .code = src.data[3],
                                 .ttl = pcp_packet_read_uint32_t(src, 4),
                                 .epoch = pcp_packet_read_uint32_t(src, 8)};
}

pcp_response_t pcp_packet_body_read_response(pcp_response_header_t header,
                                             const_view_t src) {
  switch (header.opcode) {
  case MAP:
    return (pcp_response_t){.map =
                                pcp_packet_body_read_map_response(header, src)};
  case PEER:
    return (pcp_response_t){
        .peer = pcp_packet_body_read_peer_response(header, src)};
  case ANNOUNCE:
    return (pcp_response_t){.announce.header = header};
  }
  unreachable();
}

void pcp_packet_body_read_bytes(view_t dst, const_view_t src, size_t offset) {
  memcpy(dst.data, src.data + PCP_HEADER_SIZE + offset, dst.size);
}

uint8_t pcp_packet_body_read_uint8_t(const_view_t src, size_t offset) {
  return src.data[PCP_HEADER_SIZE + offset];
}

uint16_t pcp_packet_body_read_uint16_t(const_view_t src, size_t offset) {
  return pcp_packet_read_uint16_t(src, PCP_HEADER_SIZE + offset);
}

ip_address_t pcp_packet_body_read_ip_address(const_view_t src, size_t offset) {
  return pcp_packet_read_ip_address(src, PCP_HEADER_SIZE + offset);
}

packet_t packet_create(size_t size, uint8_t const src[size]) {
  auto const data = (uint8_t *)malloc(size);
  if (data) {
    memcpy(data, src, size);
  }
  return (packet_t){.size = data ? size : 0, .data = data};
}

void packet_destroy(packet_t packet) { free(packet.data); }
