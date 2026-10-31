#include <pcp/pcp.h>

#include <string.h>
#include <unity.h>

#include "runner.h"
#include "wire.h"

static uint8_t const nonce[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

// from client 192.168.1.100, for an hour
static struct pcp_request from_client(struct pcp_request request) {
  auto const client = ip_address_from_octets(192, 168, 1, 100);
  memcpy(request.client_ip, client.addr, sizeof(request.client_ip));
  request.lifetime = 3600;
  return request;
}

// from internal port 12345
static struct pcp_request map_request(socket_address_t external) {
  auto result = from_client((struct pcp_request){
      .opcode = PCP_OPCODE_MAP,
      .body.map_body =
          {
              .protocol = UDP,
              .internal_port = 12345,
              .external_port = external.port,
          },
  });
  auto const body = &result.body.map_body;
  memcpy(body->nonce, nonce, sizeof(body->nonce));
  memcpy(body->external_ip, external.ip.addr, sizeof(body->external_ip));
  return result;
}

// from internal port 12345
static struct pcp_request peer_request(socket_address_t external,
                                       socket_address_t peer) {
  auto result = from_client((struct pcp_request){
      .opcode = PCP_OPCODE_PEER,
      .body.peer_body =
          {
              .protocol = TCP,
              .internal_port = 12345,
              .external_port = external.port,
              .peer_port = peer.port,
          },
  });
  auto const body = &result.body.peer_body;
  memcpy(body->nonce, nonce, sizeof(body->nonce));
  memcpy(body->external_ip, external.ip.addr, sizeof(body->external_ip));
  memcpy(body->peer_ip, peer.ip.addr, sizeof(body->peer_ip));
  return result;
}

static size_t write_request_with(struct pcp_request const *request,
                                 size_t count,
                                 pcp_option_t const options[count],
                                 uint8_t buffer[]) {
  // garbage, so we notice reserved fields that aren't written
  memset(buffer, 0xAA, PCP_PACKET_MAX_SIZE);

  auto const size = pcp_packet_write_request(
      (view_t){.size = PCP_PACKET_MAX_SIZE, .data = buffer}, request, count,
      options);
  TEST_ASSERT_EQUAL_size_t(pcp_packet_request_size(request, count, options),
                           size);
  return size;
}

static size_t write_request(struct pcp_request const *request,
                            uint8_t buffer[]) {
  return write_request_with(request, 0, nullptr, buffer);
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
  auto const request = peer_request(
      (socket_address_t){.ip = ip_address_from_octets(203, 0, 113, 7),
                         .port = 54321},
      (socket_address_t){.ip = ip_address_from_string("2001:db8::1").value,
                         .port = 443});

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

static pcp_option_t const third_party = {
    .type = THIRD_PARTY,
    .third_party = {.internal = {.addr = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF,
                                          0xFF, 192, 168, 1, 50}}},
};

static pcp_option_t const filter = {
    .type = FILTER,
    .filter = {.peers = {.address = {.addr = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                              0xFF, 0xFF, 198, 51, 100, 0}},
                         .prefix = 120},
               .port = 443},
};

// clang-format off
static uint8_t const third_party_bytes[] = {
    // code, reserved, length: 16
    0x01, 0x00, 0x00, 0x10,
    // internal IP address: ::ffff:192.168.1.50
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xC0, 0xA8, 0x01, 0x32};

static uint8_t const filter_bytes[] = {
    // code, reserved, length: 20
    0x03, 0x00, 0x00, 0x14,
    // reserved, prefix length: 120, remote peer port: 443
    0x00, 0x78, 0x01, 0xBB,
    // remote peer IP address: ::ffff:198.51.100.0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xC6, 0x33, 0x64, 0x00};
// clang-format on

static void test_wire_third_party_option() {
  auto const request =
      map_request((socket_address_t){.ip = ip_address_any_v4, .port = 0});

  uint8_t buffer[PCP_PACKET_MAX_SIZE];
  TEST_ASSERT_EQUAL_size_t(
      80, write_request_with(&request, 1, &third_party, buffer));

  // the option follows the MAP body, and the header's lifetime is untouched
  TEST_ASSERT_EQUAL_UINT8_ARRAY(third_party_bytes, buffer + 60,
                                sizeof(third_party_bytes));
  uint8_t const lifetime[] = {0x00, 0x00, 0x0E, 0x10};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(lifetime, buffer + 4, sizeof(lifetime));
}

static void test_wire_options_in_given_order() {
  auto const request =
      map_request((socket_address_t){.ip = ip_address_any_v4, .port = 0});
  pcp_option_t const options[] = {filter, third_party, filter};

  uint8_t buffer[PCP_PACKET_MAX_SIZE];
  TEST_ASSERT_EQUAL_size_t(60 + 24 + 20 + 24,
                           write_request_with(&request, 3, options, buffer));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(filter_bytes, buffer + 60,
                                sizeof(filter_bytes));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(third_party_bytes, buffer + 84,
                                sizeof(third_party_bytes));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(filter_bytes, buffer + 104,
                                sizeof(filter_bytes));
}

static void test_wire_delete_request_without_filters() {
  auto request =
      map_request((socket_address_t){.ip = ip_address_any_v6, .port = 0});
  request.lifetime = 0;
  pcp_option_t const options[] = {filter, third_party};

  // no FILTER in deletes, but THIRD_PARTY stays
  uint8_t buffer[PCP_PACKET_MAX_SIZE];
  TEST_ASSERT_EQUAL_size_t(80,
                           write_request_with(&request, 2, options, buffer));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(third_party_bytes, buffer + 60,
                                sizeof(third_party_bytes));
}

static void test_wire_peer_request_with_options() {
  auto const request = peer_request(
      (socket_address_t){.ip = ip_address_any_v4, .port = 0},
      (socket_address_t){.ip = ip_address_from_octets(198, 51, 100, 1),
                         .port = 443});

  uint8_t buffer[PCP_PACKET_MAX_SIZE];
  TEST_ASSERT_EQUAL_size_t(
      100, write_request_with(&request, 1, &third_party, buffer));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(third_party_bytes, buffer + 80,
                                sizeof(third_party_bytes));
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

static pcp_optional_response_t read_response(size_t size,
                                             uint8_t const data[size]) {
  return pcp_packet_read_response((const_view_t){.size = size, .data = data});
}

static void test_wire_read_map_response() {
  auto const read = read_response(sizeof(map_response), map_response);
  TEST_ASSERT_TRUE(read.has_value);
  auto const response = &read.value;

  TEST_ASSERT_EQUAL(PCP_OPCODE_MAP, response->opcode);
  TEST_ASSERT_EQUAL(SUCCESS, response->result);
  TEST_ASSERT_EQUAL_UINT32(7200, response->lifetime);
  TEST_ASSERT_EQUAL_UINT32(1000, response->epoch);

  auto const map = &response->body.map_response;
  TEST_ASSERT_EQUAL(UDP, map->protocol);
  TEST_ASSERT_EQUAL_UINT16(12345, map->internal_port);

  auto const external = ip_address_from_octets(203, 0, 113, 7);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(external.addr, map->external_ip, 16);
  TEST_ASSERT_EQUAL_UINT16(54321, map->external_port);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(nonce, map->nonce, sizeof(nonce));
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

  auto const read = read_response(sizeof(data), data);
  TEST_ASSERT_TRUE(read.has_value);
  auto const response = &read.value;

  TEST_ASSERT_EQUAL(PCP_OPCODE_PEER, response->opcode);
  auto const body = &response->body.peer_response;
  TEST_ASSERT_EQUAL(TCP, body->protocol);
  TEST_ASSERT_EQUAL_UINT16(12345, body->internal_port);
  TEST_ASSERT_EQUAL_UINT16(54321, body->external_port);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(nonce, body->nonce, sizeof(nonce));
  TEST_ASSERT_EQUAL_UINT16(443, body->peer_port);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(peer.addr, body->peer_ip, 16);
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

  auto const read = read_response(sizeof(data), data);
  TEST_ASSERT_TRUE(read.has_value);
  auto const response = &read.value;
  TEST_ASSERT_EQUAL(PCP_OPCODE_ANNOUNCE, response->opcode);
  TEST_ASSERT_EQUAL(SUCCESS, response->result);
  TEST_ASSERT_EQUAL_UINT32(0, response->lifetime);
  TEST_ASSERT_EQUAL_UINT32(5, response->epoch);
}

static void test_wire_read_announce_response_skips_options() {
  uint8_t data[32] = {0x02, 0x80};
  data[24] = 0x80; // an unknown, optional option code

  auto const read = read_response(sizeof(data), data);
  TEST_ASSERT_TRUE(read.has_value);
  auto const response = &read.value;
  TEST_ASSERT_EQUAL(PCP_OPCODE_ANNOUNCE, response->opcode);
}

static void test_wire_read_error_response() {
  uint8_t data[sizeof(map_response)];
  memcpy(data, map_response, sizeof(data));
  data[3] = NO_RESOURCES;
  data[4] = data[5] = data[6] = 0;
  data[7] = 30; // retry after 30s

  auto const read = read_response(sizeof(data), data);
  TEST_ASSERT_TRUE(read.has_value);
  auto const response = &read.value;
  TEST_ASSERT_EQUAL(NO_RESOURCES, response->result);
  TEST_ASSERT_EQUAL_UINT32(30, response->lifetime);
}

static void test_wire_read_response_skips_options() {
  uint8_t data[sizeof(map_response) + 8] = {0};
  memcpy(data, map_response, sizeof(map_response));
  data[sizeof(map_response)] = 0x80; // an unknown, optional option code

  auto const read = read_response(sizeof(data), data);
  TEST_ASSERT_TRUE(read.has_value);
  auto const response = &read.value;
  TEST_ASSERT_EQUAL_UINT16(54321, response->body.map_response.external_port);
}

// returns whether a copy of map_response broken by `modify` is still accepted
static bool read_modified(size_t size, void (*modify)(uint8_t data[])) {
  uint8_t data[PCP_PACKET_MAX_SIZE + 4] = {0};
  memcpy(data, map_response, sizeof(map_response));
  if (modify) {
    modify(data);
  }

  return read_response(size, data).has_value;
}

static void wrong_version(uint8_t data[]) { data[0] = 1; }
static void request_bit(uint8_t data[]) { data[1] = PCP_OPCODE_MAP; }
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
  RUN_TEST(test_wire_third_party_option);
  RUN_TEST(test_wire_options_in_given_order);
  RUN_TEST(test_wire_delete_request_without_filters);
  RUN_TEST(test_wire_peer_request_with_options);
  RUN_TEST(test_wire_read_map_response);
  RUN_TEST(test_wire_read_peer_response);
  RUN_TEST(test_wire_read_announce_response);
  RUN_TEST(test_wire_read_announce_response_skips_options);
  RUN_TEST(test_wire_read_error_response);
  RUN_TEST(test_wire_read_response_skips_options);
  RUN_TEST(test_wire_read_response_rejects_malformed);
}
