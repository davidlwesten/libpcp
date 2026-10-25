#include "wire.h"

#include "request.h"
#include "response.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t PCP_PACKET_HEADER_SIZE = 24;
static constexpr size_t PCP_OPTION_HEADER_SIZE = 4;

static constexpr size_t PCP_OPTION_SIZE[] = {
    [THIRD_PARTY] = PCP_OPTION_HEADER_SIZE + 16,
    [PREFER_FAILURE] = PCP_OPTION_HEADER_SIZE + 0,
    [FILTER] = PCP_OPTION_HEADER_SIZE + 20};

// requests and responses of the same opcode share their size
static constexpr size_t PCP_MESSAGE_SIZE[] = {
    [ANNOUNCE] = PCP_PACKET_HEADER_SIZE + 0,
    [MAP] = PCP_PACKET_HEADER_SIZE + 36,
    [PEER] = PCP_PACKET_HEADER_SIZE + 56};

static constexpr uint8_t PCP_VERSION = 2;
static constexpr uint8_t PCP_RESPONSE_BIT = 0x80;

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

static void
pcp_packet_body_write_bytes(view_t dst, size_t cursor, const_view_t src);
static void
pcp_packet_body_write_uint8_t(view_t dst, size_t cursor, uint8_t value);
static void
pcp_packet_body_write_uint16_t(view_t dst, size_t cursor, uint16_t value);
static void pcp_packet_body_write_ip_address(view_t dst,
                                             size_t cursor,
                                             ip_address_t const *ip);

static void pcp_packet_write_request_header(view_t dst,
                                            pcp_opcode_t opcode,
                                            pcp_time_s_t lifetime,
                                            ip_address_t const *internal) {
  assert(opcode == ANNOUNCE || opcode == MAP || opcode == PEER);
  dst.data[0] = PCP_VERSION;
  dst.data[1] = opcode; // R bit = 0 for requests
  pcp_packet_write_uint32_t(dst, 4, lifetime);
  pcp_packet_write_ip_address(dst, 8, internal);
}

static void pcp_packet_write_mapping_fields(view_t dst,
                                            pcp_request_t const *src,
                                            pcp_nonce_t const *nonce,
                                            socket_address_t const *external) {
  pcp_packet_body_write_bytes(
      dst, 0, (const_view_t){.size = sizeof(nonce->data), .data = nonce->data});
  pcp_packet_body_write_uint8_t(dst, 12, src->protocol);
  pcp_packet_body_write_uint16_t(dst, 16, src->port);
  pcp_packet_body_write_uint16_t(dst, 18, external->port);
  pcp_packet_body_write_ip_address(dst, 20, &external->ip);
}

static void pcp_packet_write_map_request(view_t dst,
                                         pcp_request_t const *src,
                                         pcp_nonce_t const *nonce,
                                         ip_address_t const *internal) {
  assert(src->type == MAP);
  pcp_packet_write_request_header(dst, MAP, src->lifetime, internal);
  pcp_packet_write_mapping_fields(dst, src, nonce, &src->map.external);
}

static void pcp_packet_write_peer_request(view_t dst,
                                          pcp_request_t const *src,
                                          pcp_nonce_t const *nonce,
                                          ip_address_t const *internal) {
  assert(src->type == PEER);
  pcp_packet_write_request_header(dst, PEER, src->lifetime, internal);
  pcp_packet_write_mapping_fields(dst, src, nonce, &src->peer.external);
  pcp_packet_body_write_uint16_t(dst, 36, src->peer.peer.port);
  pcp_packet_body_write_ip_address(dst, 40, &src->peer.peer.ip);
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
                                pcp_nonce_t const *nonce,
                                const ip_address_t *internal,
                                size_t count,
                                pcp_option_t const options[count]) {
  assert(src->type == MAP || src->type == PEER);
  assert(dst.size >= PCP_MESSAGE_SIZE[src->type]);
  assert(nonce);

  // takes care of every reserved field, so the writers can skip them
  memset(dst.data, 0, PCP_MESSAGE_SIZE[src->type]);

  (src->type == MAP) ? pcp_packet_write_map_request(dst, src, nonce, internal)
                     : pcp_packet_write_peer_request(dst, src, nonce, internal);

  size_t cursor = PCP_MESSAGE_SIZE[src->type];

  auto const begin = options;
  auto const end = options + count;
  for (auto iter = begin; iter != end; ++iter) {
    pcp_packet_write_option(dst, cursor, iter);
    cursor += PCP_OPTION_SIZE[iter->type];
  }

  return cursor;
}

static void
pcp_packet_body_write_bytes(view_t dst, size_t cursor, const_view_t src) {
  assert(dst.size >= (PCP_PACKET_HEADER_SIZE + cursor + src.size));
  memcpy(dst.data + PCP_PACKET_HEADER_SIZE + cursor, src.data, src.size);
}

static void
pcp_packet_body_write_uint8_t(view_t dst, size_t cursor, uint8_t value) {
  assert(dst.size > (PCP_PACKET_HEADER_SIZE + cursor));
  dst.data[PCP_PACKET_HEADER_SIZE + cursor] = value;
}

static void
pcp_packet_body_write_uint16_t(view_t dst, size_t cursor, uint16_t value) {
  pcp_packet_write_uint16_t(dst, PCP_PACKET_HEADER_SIZE + cursor, value);
}

static void pcp_packet_body_write_ip_address(view_t dst,
                                             size_t cursor,
                                             ip_address_t const *ip) {
  pcp_packet_write_ip_address(dst, PCP_PACKET_HEADER_SIZE + cursor, ip);
}

static uint16_t pcp_packet_read_uint16_t(const_view_t src, size_t cursor) {
  assert(src.size >= (cursor + 2));
  return (uint16_t)(src.data[cursor + 0] << 8 | src.data[cursor + 1]);
}

static uint32_t pcp_packet_read_uint32_t(const_view_t src, size_t cursor) {
  assert(src.size >= (cursor + 4));
  return (uint32_t)src.data[cursor + 0] << 24 |
         (uint32_t)src.data[cursor + 1] << 16 |
         (uint32_t)src.data[cursor + 2] << 8 | (uint32_t)src.data[cursor + 3];
}

static ip_address_t pcp_packet_read_ip_address(const_view_t src,
                                               size_t cursor) {
  assert(src.size >= (cursor + sizeof(ip_address_t)));
  ip_address_t result = {0};
  memcpy(result.addr, src.data + cursor, sizeof(result.addr));
  return result;
}

static socket_address_t pcp_packet_body_read_socket_address(const_view_t src,
                                                            size_t port_cursor,
                                                            size_t ip_cursor) {
  return (socket_address_t){
      .ip = pcp_packet_read_ip_address(src, PCP_PACKET_HEADER_SIZE + ip_cursor),
      .port =
          pcp_packet_read_uint16_t(src, PCP_PACKET_HEADER_SIZE + port_cursor),
  };
}

bool pcp_packet_read_response(const_view_t src, pcp_response_t *out) {
  assert(src.data);
  assert(out);

  // RFC 6887 §8.3: anything else must be silently ignored
  if (src.size < PCP_PACKET_HEADER_SIZE || src.size > PCP_PACKET_MAX_SIZE ||
      src.size % 4 != 0) {
    return false;
  }

  if (src.data[0] != PCP_VERSION || !(src.data[1] & PCP_RESPONSE_BIT)) {
    return false;
  }

  // only opcodes we send requests for can have a matching response. This also
  // rejects error responses the server couldn't attach a body (and therefore a
  // nonce) to, which couldn't be matched to a mapping anyway
  auto const opcode = (pcp_opcode_t)(src.data[1] & ~PCP_RESPONSE_BIT);
  if ((opcode != MAP && opcode != PEER) ||
      src.size < PCP_MESSAGE_SIZE[opcode]) {
    return false;
  }

  *out = (pcp_response_t){
      .type = opcode,
      .result = (pcp_result_t)src.data[3],
      .lifetime = pcp_packet_read_uint32_t(src, 4),
      .epoch = pcp_packet_read_uint32_t(src, 8),
      .protocol = (pcp_protocol_t)src.data[PCP_PACKET_HEADER_SIZE + 12],
      .port = pcp_packet_read_uint16_t(src, PCP_PACKET_HEADER_SIZE + 16),
  };
  memcpy(out->nonce.data, src.data + PCP_PACKET_HEADER_SIZE,
         sizeof(out->nonce.data));

  // the first 36 bytes of the MAP and PEER bodies share their layout
  auto const external = pcp_packet_body_read_socket_address(src, 18, 20);
  if (opcode == MAP) {
    out->map.external = external;
  } else {
    out->peer.external = external;
    out->peer.peer = pcp_packet_body_read_socket_address(src, 36, 40);
  }

  return true;
}
