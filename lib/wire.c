#include "wire.h"

#include "request.h"
#include "response.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t PCP_PACKET_HEADER_SIZE = 24;
static constexpr size_t PCP_OPTION_HEADER_SIZE = 4;

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

// length of the option data, not counting the 4-byte header
static uint16_t pcp_option_length(pcp_option_t const *option) {
  switch (option->type) {
  case THIRD_PARTY:
    return 16;
  case FILTER:
    return 20;
  }
}

// Deletes (lifetime 0) can't carry FILTER. THIRD_PARTY has to stay though:
// without it the server looks for a mapping on our own address, doesn't find
// one and still answers SUCCESS.
static bool pcp_option_included(pcp_request_t const *request,
                                pcp_option_t const *option) {
  return option->type != FILTER || request->lifetime > 0;
}

static size_t pcp_option_size(pcp_option_t const *option) {
  return PCP_OPTION_HEADER_SIZE + pcp_option_length(option);
}

size_t pcp_packet_request_size(pcp_request_t const *request,
                               size_t count,
                               pcp_option_t const options[count]) {
  assert(request);
  assert(request->type == MAP || request->type == PEER);
  assert(count == 0 || options);

  size_t size = PCP_MESSAGE_SIZE[request->type];
  for (size_t i = 0; i < count; ++i) {
    if (pcp_option_included(request, &options[i])) {
      size += pcp_option_size(&options[i]);
    }
  }
  return size;
}

// returns the offset after the option
static size_t
pcp_packet_write_option(view_t dst, size_t cursor, pcp_option_t const *option) {
  auto const length = pcp_option_length(option);
  assert(dst.size >= cursor + PCP_OPTION_HEADER_SIZE + length);

  // header: code, reserved, length
  dst.data[cursor] = (uint8_t)option->type;
  pcp_packet_write_uint16_t(dst, cursor + 2, length);

  auto const data = cursor + PCP_OPTION_HEADER_SIZE;
  switch (option->type) {
  case THIRD_PARTY:
    pcp_packet_write_ip_address(dst, data, &option->third_party.internal);
    break;
  case FILTER:
    // reserved, prefix length, remote peer port, remote peer IP address
    dst.data[data + 1] = option->filter.peers.prefix;
    pcp_packet_write_uint16_t(dst, data + 2, option->filter.port);
    pcp_packet_write_ip_address(dst, data + 4, &option->filter.peers.address);
    break;
  }

  return data + length;
}

size_t pcp_packet_write_request(view_t dst,
                                const pcp_request_t *src,
                                pcp_nonce_t const *nonce,
                                const ip_address_t *internal,
                                size_t count,
                                pcp_option_t const options[count]) {
  assert(src->type == MAP || src->type == PEER);
  assert(nonce);

  auto const size = pcp_packet_request_size(src, count, options);
  assert(dst.size >= size);

  // zero everything so reserved fields don't need writing
  memset(dst.data, 0, size);

  switch (src->type) {
  case MAP:
    pcp_packet_write_map_request(dst, src, nonce, internal);
    break;
  case PEER:
    pcp_packet_write_peer_request(dst, src, nonce, internal);
    break;
  case ANNOUNCE:
    unreachable();
  }

  size_t cursor = PCP_MESSAGE_SIZE[src->type];
  for (size_t i = 0; i < count; ++i) {
    if (pcp_option_included(src, &options[i])) {
      cursor = pcp_packet_write_option(dst, cursor, &options[i]);
    }
  }

  assert(cursor == size);
  return size;
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

static pcp_nonce_t pcp_packet_body_read_nonce(const_view_t src) {
  pcp_nonce_t result = {0};
  memcpy(result.data, src.data + PCP_PACKET_HEADER_SIZE, sizeof(result.data));
  return result;
}

bool pcp_packet_read_response(const_view_t src, pcp_response_t *out) {
  assert(src.data);
  assert(out);

  // silently drop anything malformed (RFC 6887 section 8.3)
  if (src.size < PCP_PACKET_HEADER_SIZE || src.size > PCP_PACKET_MAX_SIZE ||
      src.size % 4 != 0) {
    return false;
  }

  if (src.data[0] != PCP_VERSION || !(src.data[1] & PCP_RESPONSE_BIT)) {
    return false;
  }

  // only ANNOUNCE and responses to our own MAP/PEER requests matter. Errors
  // without a body can't be matched to a mapping anyway.
  auto const opcode = (pcp_opcode_t)(src.data[1] & ~PCP_RESPONSE_BIT);
  if ((opcode != ANNOUNCE && opcode != MAP && opcode != PEER) ||
      src.size < PCP_MESSAGE_SIZE[opcode]) {
    return false;
  }

  *out = (pcp_response_t){
      .type = opcode,
      .result = (pcp_result_t)src.data[3],
      .lifetime = pcp_packet_read_uint32_t(src, 4),
      .epoch = pcp_packet_read_uint32_t(src, 8),
  };

  switch (opcode) {
  case ANNOUNCE:
    break;
  case MAP:
    out->map.nonce = pcp_packet_body_read_nonce(src);
    out->map.protocol = (pcp_protocol_t)src.data[PCP_PACKET_HEADER_SIZE + 12];
    out->map.port = pcp_packet_read_uint16_t(src, PCP_PACKET_HEADER_SIZE + 16);
    out->map.external = pcp_packet_body_read_socket_address(src, 18, 20);
    break;
  case PEER:
    out->peer.nonce = pcp_packet_body_read_nonce(src);
    out->peer.protocol = (pcp_protocol_t)src.data[PCP_PACKET_HEADER_SIZE + 12];
    out->peer.port = pcp_packet_read_uint16_t(src, PCP_PACKET_HEADER_SIZE + 16);
    out->peer.external = pcp_packet_body_read_socket_address(src, 18, 20);
    out->peer.peer = pcp_packet_body_read_socket_address(src, 36, 40);
    break;
  }

  return true;
}
