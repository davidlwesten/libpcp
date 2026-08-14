#include "datagram.h"

#include <assert.h>
#include <string.h>

static constexpr uint8_t VERSION = 2;

static void pcp_datagram_write_uint16_t(pcp_datagram_t *dst,
                                        size_t offset,
                                        uint16_t value) {
  dst->data[offset + 0] = (uint8_t)(value >> 8);
  dst->data[offset + 1] = (uint8_t)(value & 0xFF);
}

static void pcp_datagram_write_uint32_t(pcp_datagram_t *dst,
                                        size_t offset,
                                        uint32_t value) {
  dst->data[offset + 0] = (uint8_t)(value >> 24);
  dst->data[offset + 1] = (uint8_t)(value >> 16);
  dst->data[offset + 2] = (uint8_t)(value >> 8);
  dst->data[offset + 3] = (uint8_t)(value & 0xFF);
}

static void pcp_datagram_write_ip_address(pcp_datagram_t *dst,
                                          size_t offset,
                                          const ip_address_t ip) {
  memcpy(dst->data + offset, ip.addr, sizeof(ip.addr));
}

static uint16_t pcp_datagram_read_uint16_t(const pcp_datagram_t *src,
                                           size_t offset) {
  return ((uint16_t)src->data[offset + 0] << 8) |
         ((uint16_t)src->data[offset + 1]);
}

static uint32_t pcp_datagram_read_uint32_t(const pcp_datagram_t *src,
                                           size_t offset) {
  return ((uint32_t)src->data[offset + 0] << 24) |
         ((uint32_t)src->data[offset + 1] << 16) |
         ((uint32_t)src->data[offset + 2] << 8) |
         ((uint32_t)src->data[offset + 3]);
}

static ip_address_t pcp_datagram_read_ip_address(const pcp_datagram_t *src,
                                                 size_t offset) {
  ip_address_t result = {0};
  memcpy(result.addr, src->data + offset, sizeof(result.addr));
  return result;
}

void pcp_datagram_write_request_header(pcp_datagram_t *dst,
                                       pcp_opcode_t opcode,
                                       time_t ttl,
                                       ip_address_t local) {
  assert(dst);
  assert(opcode == ANNOUNCE || opcode == MAP || opcode == PEER);
  dst->data[0] = VERSION;
  dst->data[1] = opcode;
  pcp_datagram_write_uint32_t(dst, 4, (uint32_t)ttl);
  pcp_datagram_write_ip_address(dst, 8, local);
}

void pcp_datagram_body_write_bytes(pcp_datagram_t *dst,
                                   size_t offset,
                                   size_t size,
                                   const uint8_t src[size]) {
  memcpy(dst->data + PCP_HEADER_SIZE + offset, src, size);
}

void pcp_datagram_body_write_uint8_t(pcp_datagram_t *dst,
                                     size_t offset,
                                     uint8_t value) {
  dst->data[PCP_HEADER_SIZE + offset] = value;
}

void pcp_datagram_body_write_uint16_t(pcp_datagram_t *dst,
                                      size_t offset,
                                      uint16_t value) {
  pcp_datagram_write_uint16_t(dst, PCP_HEADER_SIZE + offset, value);
}

void pcp_datagram_body_write_ip_address(pcp_datagram_t *dst,
                                        size_t offset,
                                        ip_address_t ip) {
  pcp_datagram_write_ip_address(dst, PCP_HEADER_SIZE + offset, ip);
}

void pcp_datagram_read_request_header(pcp_opcode_t *opcode,
                                      time_t *ttl,
                                      ip_address_t *client_ip,
                                      const pcp_datagram_t *src) {
  assert(src->data[0] == VERSION);
  assert(!(src->data[1] & 0x80));
  *opcode = (src->data[1] >> 1);
  assert(*opcode == ANNOUNCE || *opcode == MAP || *opcode == PEER);
  *ttl = pcp_datagram_read_uint32_t(src, 4);
  *client_ip = pcp_datagram_read_ip_address(src, 8);
}

void pcp_datagram_read_response_header(pcp_opcode_t *opcode,
                                       pcp_result_t *code,
                                       time_t *ttl,
                                       time_t *epoch,
                                       const pcp_datagram_t *src) {
  assert(src->data[0] == VERSION);
  assert((src->data[1] & 0x80));
  *opcode = (src->data[1] & 0x7F);
  assert(*opcode == ANNOUNCE || *opcode == MAP || *opcode == PEER);
  *code = src->data[3];
  *ttl = pcp_datagram_read_uint32_t(src, 4);
  *epoch = pcp_datagram_read_uint32_t(src, 8);
}

void pcp_datagram_body_read_bytes(size_t size,
                                  uint8_t dst[size],
                                  const pcp_datagram_t *src,
                                  size_t offset) {
  memcpy(dst, src->data + PCP_HEADER_SIZE + offset, size);
}

uint8_t pcp_datagram_body_read_uint8_t(const pcp_datagram_t *src,
                                       size_t offset) {
  return src->data[PCP_HEADER_SIZE + offset];
}

uint16_t pcp_datagram_body_read_uint16_t(const pcp_datagram_t *src,
                                         size_t offset) {
  return pcp_datagram_read_uint16_t(src, PCP_HEADER_SIZE + offset);
}

ip_address_t pcp_datagram_body_read_ip_address(const pcp_datagram_t *src,
                                               size_t offset) {
  return pcp_datagram_read_ip_address(src, PCP_HEADER_SIZE + offset);
}
