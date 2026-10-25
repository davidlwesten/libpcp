#include "wire.h"

#include "request.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t PCP_PACKET_HEADER_SIZE = 24;
static constexpr size_t PCP_OPTION_HEADER_SIZE = 4;

static constexpr size_t PCP_OPTION_SIZE[] = {
    [THIRD_PARTY] = PCP_OPTION_HEADER_SIZE + 16,
    [PREFER_FAILURE] = PCP_OPTION_HEADER_SIZE + 0,
    [FILTER] = PCP_OPTION_HEADER_SIZE + 20};

static constexpr size_t PCP_REQUEST_SIZE[] = {
    [ANNOUNCE] = PCP_PACKET_HEADER_SIZE + 0,
    [MAP] = PCP_PACKET_HEADER_SIZE + 36,
    [PEER] = PCP_PACKET_HEADER_SIZE + 56};

static constexpr uint8_t PCP_VERSION = 2;

static void
pcp_packet_write_uint16_t(view_t dst, size_t cursor, uint16_t value) {
  assert(dst.size >= (cursor + 2));
  dst.data[cursor + 0] = (uint8_t)(value >> 8);
  dst.data[cursor + 1] = (uint8_t)(value & 0xFF);
}

static void
pcp_packet_write_uint32_t(view_t dst, size_t cursor, uint32_t value) {
  assert(dst.size >= (cursor + 4));
  dst.data[cursor + 0] = (uint8_t)(value >> 24);
  dst.data[cursor + 1] = (uint8_t)(value >> 16);
  dst.data[cursor + 2] = (uint8_t)(value >> 8);
  dst.data[cursor + 3] = (uint8_t)(value & 0xFF);
}

static void
pcp_packet_write_ip_address(view_t dst, size_t cursor, const ip_address_t *ip) {
  assert(dst.size >= (cursor + sizeof(ip_address_t)));
  memcpy(dst.data + cursor, ip->addr, sizeof(ip->addr));
}

static void pcp_packet_write_request_header(view_t dst,
                                            pcp_opcode_t opcode,
                                            time_t lifetime,
                                            ip_address_t const *internal) {
  assert(opcode == ANNOUNCE || opcode == MAP || opcode == PEER);
  dst.data[0] = PCP_VERSION;
  dst.data[1] = opcode;
  pcp_packet_write_uint32_t(dst, 4, (uint32_t)lifetime);
  pcp_packet_write_ip_address(dst, 8, internal);
}

static void pcp_packet_write_map_request(view_t dst,
                                         time_t lifetime,
                                         ip_address_t const *internal) {
  pcp_packet_write_request_header(dst, MAP, lifetime, internal);
}

static void pcp_packet_write_peer_request(view_t dst,
                                          time_t lifetime,
                                          ip_address_t const *internal) {
  pcp_packet_write_request_header(dst, PEER, lifetime, internal);
}

static void pcp_packet_write_option_header(view_t dst,
                                           size_t cursor,
                                           uint8_t code,
                                           uint16_t length) {
  dst.data[cursor] = code;
  pcp_packet_write_uint16_t(dst, cursor + 2, length);
}

static void pcp_packet_write_third_party_option(view_t dst,
                                                size_t cursor,
                                                ip_address_t const *internal) {
  assert(dst.size >= (cursor + PCP_OPTION_SIZE[THIRD_PARTY]));
  pcp_packet_write_option_header(dst, cursor, THIRD_PARTY,
                                 PCP_OPTION_SIZE[THIRD_PARTY]);
  pcp_packet_write_ip_address(dst, 4, internal);
}

static void pcp_packet_write_prefer_failure_option(view_t /*dst*/,
                                                   size_t /*cursor*/) {}

static void pcp_packet_write_filter_option(view_t /*dst*/,
                                           size_t /*cursor*/,
                                           socket_address_t const * /*peer*/,
                                           uint8_t /*prefix*/) {}

static void
pcp_packet_write_option(view_t dst, size_t cursor, pcp_option_t const *option) {
  assert(option);
  assert(option->type == THIRD_PARTY || option->type == PREFER_FAILURE ||
         option->type == FILTER);

  switch (option->type) {
  case THIRD_PARTY:
    pcp_packet_write_third_party_option(dst, cursor,
                                        &option->third_party.internal);
    break;
  case PREFER_FAILURE:
    pcp_packet_write_prefer_failure_option(dst, cursor);
    break;
  case FILTER:
    pcp_packet_write_filter_option(dst, cursor, &option->filter.peer,
                                   option->filter.prefix);
    break;
  }
}

size_t pcp_packet_write_request(view_t dst,
                                const pcp_request_t *src,
                                const ip_address_t *internal,
                                size_t count,
                                pcp_option_t const options[count]) {
  assert(src->type == MAP || src->type == PEER);

  (src->type == MAP)
      ? pcp_packet_write_map_request(dst, src->lifetime, internal)
      : pcp_packet_write_peer_request(dst, src->lifetime, internal);

  size_t cursor = PCP_REQUEST_SIZE[src->type];

  auto const begin = options;
  auto const end = options + count;
  for (auto iter = begin; iter != end; ++iter) {
    pcp_packet_write_option(dst, cursor, iter);
    cursor += PCP_OPTION_SIZE[iter->type];
  }

  return cursor;
}

void pcp_packet_body_write_bytes(view_t dst, size_t cursor, const_view_t src) {
  assert(dst.size >= (PCP_PACKET_HEADER_SIZE + cursor + src.size));
  memcpy(dst.data + PCP_PACKET_HEADER_SIZE + cursor, src.data, src.size);
}

void pcp_packet_body_write_uint8_t(view_t dst, size_t cursor, uint8_t value) {
  assert(dst.size >= (PCP_PACKET_HEADER_SIZE + cursor));
  dst.data[PCP_PACKET_HEADER_SIZE + cursor] = value;
}

void pcp_packet_body_write_uint16_t(view_t dst, size_t cursor, uint16_t value) {
  pcp_packet_write_uint16_t(dst, PCP_PACKET_HEADER_SIZE + cursor, value);
}

void pcp_packet_body_write_ip_address(view_t dst,
                                      size_t cursor,
                                      ip_address_t const *ip) {
  pcp_packet_write_ip_address(dst, PCP_PACKET_HEADER_SIZE + cursor, ip);
}
