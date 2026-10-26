#include <pcp/pcp.h>

#include <string.h>
#include <unity.h>

#include "request.h"
#include "response.h"
#include "runner.h"
#include "wire.h"

static pcp_nonce_t const nonce = {
    .data = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};

static pcp_request_t map_request(socket_address_t external) {
  return (pcp_request_t){
      .lifetime = 3600,
      .protocol = UDP,
      .port = 12345,
      .type = MAP,
      .map = {.external = external},
  };
}

static size_t write_request(pcp_request_t const *request, uint8_t buffer[]) {
  // garbage, so we notice reserved fields that aren't written
  memset(buffer, 0xAA, PCP_PACKET_MAX_SIZE);

  auto const internal = ip_address_from_octets(192, 168, 1, 100);
  return pcp_packet_write_request(
      (view_t){.size = PCP_PACKET_MAX_SIZE, .data = buffer}, request, &nonce,
      &internal, 0, nullptr);
}

static void test_wire_map_request() {
  auto const request = map_request((socket_address_t){
      .ip = ip_address_from_octets(203, 0, 113, 7),
      .port = 54321,
  });

  uint8_t buffer[PCP_PACKET_MAX_SIZE];
  auto const size = write_request(&request, buffer);

  // clang-format off
  uint8_t const expected[] = {
      // header: version, R + opcode, reserved
      0x02, 0x01, 0x00, 0x00,
      // requested lifetime: 3600
      0x00, 0x00, 0x0E, 0x10,
      // client IP address: ::ffff:192.168.1.100
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
      0xC0, 0xA8, 0x01, 0x64,
      // MAP: nonce
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
      // protocol, reserved
      0x11, 0x00, 0x00, 0x00,
      // internal port: 12345, suggested external port: 54321
      0x30, 0x39, 0xD4, 0x31,
      // suggested external IP address: ::ffff:203.0.113.7
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
      0xCB, 0x00, 0x71, 0x07};
  // clang-format on

  TEST_ASSERT_EQUAL_size_t(sizeof(expected), size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, sizeof(expected));
}

static void test_wire_map_request_without_suggestion() {
  auto const request =
      map_request((socket_address_t){.ip = ip_address_any_v4, .port = 0});

  uint8_t buffer[PCP_PACKET_MAX_SIZE];
  TEST_ASSERT_EQUAL_size_t(60, write_request(&request, buffer));

  // suggested external port and IP address: no preference, IPv4
  uint8_t const expected[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer + 42, sizeof(expected));
}

static void test_wire_peer_request() {
  pcp_request_t const request = {
      .lifetime = 3600,
      .protocol = TCP,
      .port = 12345,
      .type = PEER,
      .peer =
          {
              .external = {.ip = ip_address_from_octets(203, 0, 113, 7),
                           .port = 54321},
              .peer = {.ip = ip_address_from_string("2001:db8::1").value,
                       .port = 443},
          },
  };

  uint8_t buffer[PCP_PACKET_MAX_SIZE];
  auto const size = write_request(&request, buffer);

  // clang-format off
  uint8_t const expected[] = {
      // header: version, R + opcode, reserved
      0x02, 0x02, 0x00, 0x00,
      // requested lifetime: 3600
      0x00, 0x00, 0x0E, 0x10,
      // client IP address: ::ffff:192.168.1.100
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
      0xC0, 0xA8, 0x01, 0x64,
      // PEER: nonce
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
      // protocol, reserved
      0x06, 0x00, 0x00, 0x00,
      // internal port: 12345, suggested external port: 54321
      0x30, 0x39, 0xD4, 0x31,
      // suggested external IP address: ::ffff:203.0.113.7
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
      0xCB, 0x00, 0x71, 0x07,
      // remote peer port: 443, reserved
      0x01, 0xBB, 0x00, 0x00,
      // remote peer IP address: 2001:db8::1
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x01};
  // clang-format on

  TEST_ASSERT_EQUAL_size_t(sizeof(expected), size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, sizeof(expected));
}

// clang-format off
static uint8_t const map_response[] = {
    // header: version, R + opcode, reserved, result code
    0x02, 0x81, 0x00, 0x00,
    // lifetime: 7200
    0x00, 0x00, 0x1C, 0x20,
    // epoch time: 1000
    0x00, 0x00, 0x03, 0xE8,
    // reserved
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // MAP: nonce
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    // protocol, reserved
    0x11, 0x00, 0x00, 0x00,
    // internal port: 12345, assigned external port: 54321
    0x30, 0x39, 0xD4, 0x31,
    // assigned external IP address: ::ffff:203.0.113.7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xCB, 0x00, 0x71, 0x07};
// clang-format on

static bool
read_response(size_t size, uint8_t const data[size], pcp_response_t *out) {
  return pcp_packet_read_response((const_view_t){.size = size, .data = data},
                                  out);
}

static void test_wire_read_map_response() {
  pcp_response_t response = {0};
  TEST_ASSERT_TRUE(
      read_response(sizeof(map_response), map_response, &response));

  TEST_ASSERT_EQUAL(MAP, response.type);
  TEST_ASSERT_EQUAL(SUCCESS, response.result);
  TEST_ASSERT_EQUAL_UINT32(7200, response.lifetime);
  TEST_ASSERT_EQUAL_UINT32(1000, response.epoch);
  TEST_ASSERT_EQUAL(UDP, response.map.protocol);
  TEST_ASSERT_EQUAL_UINT16(12345, response.map.port);

  auto const external = ip_address_from_octets(203, 0, 113, 7);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(external.addr, response.map.external.ip.addr,
                                16);
  TEST_ASSERT_EQUAL_UINT16(54321, response.map.external.port);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(nonce.data, response.map.nonce.data,
                                sizeof(nonce.data));
}

static void test_wire_read_peer_response() {
  uint8_t data[80] = {0};
  memcpy(data, map_response, sizeof(map_response));
  data[1] = 0x82;  // R + PEER
  data[36] = 0x06; // TCP
  // remote peer port: 443, reserved
  data[60] = 0x01;
  data[61] = 0xBB;
  // remote peer IP address: 2001:db8::1
  auto const peer = ip_address_from_string("2001:db8::1").value;
  memcpy(data + 64, peer.addr, sizeof(peer.addr));

  pcp_response_t response = {0};
  TEST_ASSERT_TRUE(read_response(sizeof(data), data, &response));

  TEST_ASSERT_EQUAL(PEER, response.type);
  TEST_ASSERT_EQUAL(TCP, response.peer.protocol);
  TEST_ASSERT_EQUAL_UINT16(12345, response.peer.port);
  TEST_ASSERT_EQUAL_UINT16(54321, response.peer.external.port);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(nonce.data, response.peer.nonce.data,
                                sizeof(nonce.data));
  TEST_ASSERT_EQUAL_UINT16(443, response.peer.peer.port);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(peer.addr, response.peer.peer.ip.addr, 16);
}

static void test_wire_read_announce_response() {
  // clang-format off
  uint8_t const data[] = {
      // header: version, R + opcode, reserved, result code
      0x02, 0x80, 0x00, 0x00,
      // lifetime: 0
      0x00, 0x00, 0x00, 0x00,
      // epoch time: 5
      0x00, 0x00, 0x00, 0x05,
      // reserved
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  // clang-format on

  pcp_response_t response = {0};
  TEST_ASSERT_TRUE(read_response(sizeof(data), data, &response));
  TEST_ASSERT_EQUAL(ANNOUNCE, response.type);
  TEST_ASSERT_EQUAL(SUCCESS, response.result);
  TEST_ASSERT_EQUAL_UINT32(0, response.lifetime);
  TEST_ASSERT_EQUAL_UINT32(5, response.epoch);
}

static void test_wire_read_announce_response_skips_options() {
  uint8_t data[32] = {0x02, 0x80};
  data[24] = 0x80; // an unknown, optional option code

  pcp_response_t response = {0};
  TEST_ASSERT_TRUE(read_response(sizeof(data), data, &response));
  TEST_ASSERT_EQUAL(ANNOUNCE, response.type);
}

static void test_wire_read_error_response() {
  uint8_t data[sizeof(map_response)];
  memcpy(data, map_response, sizeof(data));
  data[3] = NO_RESOURCES;
  data[4] = data[5] = data[6] = 0;
  data[7] = 30; // retry after 30s

  pcp_response_t response = {0};
  TEST_ASSERT_TRUE(read_response(sizeof(data), data, &response));
  TEST_ASSERT_EQUAL(NO_RESOURCES, response.result);
  TEST_ASSERT_EQUAL_UINT32(30, response.lifetime);
}

static void test_wire_read_response_skips_options() {
  uint8_t data[sizeof(map_response) + 8] = {0};
  memcpy(data, map_response, sizeof(map_response));
  data[sizeof(map_response)] = 0x80; // an unknown, optional option code

  pcp_response_t response = {0};
  TEST_ASSERT_TRUE(read_response(sizeof(data), data, &response));
  TEST_ASSERT_EQUAL_UINT16(54321, response.map.external.port);
}

// returns whether a copy of map_response broken by `modify` is still accepted
static bool read_modified(size_t size, void (*modify)(uint8_t data[])) {
  uint8_t data[PCP_PACKET_MAX_SIZE + 4] = {0};
  memcpy(data, map_response, sizeof(map_response));
  if (modify) {
    modify(data);
  }

  pcp_response_t response = {0};
  return read_response(size, data, &response);
}

static void wrong_version(uint8_t data[]) { data[0] = 1; }
static void request_bit(uint8_t data[]) { data[1] = MAP; }
static void unknown_opcode(uint8_t data[]) { data[1] = 0x83; }
static void peer_opcode(uint8_t data[]) { data[1] = 0x82; }

static void test_wire_read_response_rejects_malformed() {
  TEST_ASSERT_TRUE(read_modified(sizeof(map_response), nullptr));

  // sizes
  TEST_ASSERT_FALSE(read_modified(0, nullptr));
  TEST_ASSERT_FALSE(read_modified(20, nullptr));
  TEST_ASSERT_FALSE(read_modified(sizeof(map_response) + 2, nullptr));
  TEST_ASSERT_FALSE(read_modified(PCP_PACKET_MAX_SIZE + 4, nullptr));

  // header
  TEST_ASSERT_FALSE(read_modified(sizeof(map_response), wrong_version));
  TEST_ASSERT_FALSE(read_modified(sizeof(map_response), request_bit));
  TEST_ASSERT_FALSE(read_modified(sizeof(map_response), unknown_opcode));

  // a body too short for its opcode, e.g. an error without the request's body
  TEST_ASSERT_FALSE(read_modified(24, nullptr));
  TEST_ASSERT_FALSE(read_modified(sizeof(map_response), peer_opcode));
}

void wire_tests() {
  RUN_TEST(test_wire_map_request);
  RUN_TEST(test_wire_map_request_without_suggestion);
  RUN_TEST(test_wire_peer_request);
  RUN_TEST(test_wire_read_map_response);
  RUN_TEST(test_wire_read_peer_response);
  RUN_TEST(test_wire_read_announce_response);
  RUN_TEST(test_wire_read_announce_response_skips_options);
  RUN_TEST(test_wire_read_error_response);
  RUN_TEST(test_wire_read_response_skips_options);
  RUN_TEST(test_wire_read_response_rejects_malformed);
}
