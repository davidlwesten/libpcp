#include <pcp/pcp.h>

#include <string.h>
#include <unity.h>

#include "rng.h"
#include "runner.h"
#include "session.h"

static uint16_t rng_value;
static uint8_t send_buffer[PCP_PACKET_MAX_SIZE];
static pcp_session_t *session;

static pcp_session_t *begin(uint16_t value) {
  rng_value = value;
  session = pcp_session_begin((pcp_config_t){
      .local = ip_address_loopback_v4,
      .allocator = {0},
      .rng = {.random = test_rng_fixed, .ctx = &rng_value},
      .buffer = {.size = sizeof(send_buffer), .data = send_buffer},
  });
  TEST_ASSERT_NOT_NULL(session);
  return session;
}

static void end() {
  pcp_session_end(session);
  session = nullptr;
}

static pcp_handle_t map() {
  auto const handle =
      pcp_session_map_request(session, 3600, UDP, 1234, 0, nullptr);
  TEST_ASSERT_TRUE(handle.has_value);
  return handle.value;
}

// the deadline from the last PCP_EVENT_TIMEOUT seen by send_due
static pcp_time_ms_t deadline;

// polls at `now` like an application would until told to wait, returning the
// number of requests sent
static size_t send_due(pcp_time_ms_t now) {
  size_t sent = 0;
  while (true) {
    auto const event = pcp_session_poll(session, now);
    if (event.type == PCP_EVENT_TIMEOUT) {
      // every test using this has a mapping that's waiting on a timer, and the
      // deadline must lie in the future or the application would spin
      TEST_ASSERT_TRUE(event.timeout.deadline.has_value);
      TEST_ASSERT_GREATER_THAN_UINT64(now, event.timeout.deadline.value);
      deadline = event.timeout.deadline.value;
      return sent;
    }

    TEST_ASSERT_EQUAL(PCP_EVENT_SEND, event.type);
    TEST_ASSERT_EQUAL_PTR(send_buffer, event.send.data);
    TEST_ASSERT_GREATER_THAN_size_t(0, event.send.size);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(sizeof(send_buffer), event.send.size);
    ++sent;
  }
}

static void test_session_never_without_mappings() {
  begin(100);
  auto const event = pcp_session_poll(session, 0);
  TEST_ASSERT_EQUAL(PCP_EVENT_TIMEOUT, event.type);
  TEST_ASSERT_FALSE(event.timeout.deadline.has_value);
  end();
}

static void test_session_new_request_due_immediately() {
  begin(100);
  map();

  // due whatever the application's clock says
  TEST_ASSERT_EQUAL_size_t(1, send_due(1'000'000));
  TEST_ASSERT_EQUAL_size_t(0, send_due(1'000'000));
  end();
}

static void test_session_no_send_before_due() {
  begin(100);
  map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(1'000));
  TEST_ASSERT_EQUAL_size_t(0, send_due(3'999));
  end();
}

static void test_session_send_writes_into_config_buffer() {
  begin(100);
  memset(send_buffer, 0xAA, sizeof(send_buffer));
  map();

  auto const event = pcp_session_poll(session, 0);
  TEST_ASSERT_EQUAL(PCP_EVENT_SEND, event.type);
  TEST_ASSERT_EQUAL_PTR(send_buffer, event.send.data);

  // the request header starts with the version and opcode
  TEST_ASSERT_EQUAL_UINT8(2, send_buffer[0]);
  TEST_ASSERT_EQUAL_UINT8(MAP, send_buffer[1]);
  end();
}

static void test_session_retransmits_on_schedule() {
  begin(100); // jitter 0
  map();

  // the RT counts from when the send event was polled
  TEST_ASSERT_EQUAL_size_t(1, send_due(1'200));
  TEST_ASSERT_EQUAL_UINT64(4'200, deadline);
  TEST_ASSERT_EQUAL_size_t(0, send_due(1'200));
  TEST_ASSERT_EQUAL_UINT64(4'200, deadline);

  TEST_ASSERT_EQUAL_size_t(0, send_due(4'199));
  TEST_ASSERT_EQUAL_size_t(1, send_due(4'200));
  TEST_ASSERT_EQUAL_UINT64(10'200, deadline);
  end();
}

static void test_session_jitter_applied() {
  begin(200); // jitter +10%
  map();

  TEST_ASSERT_EQUAL_size_t(1, send_due(0));
  TEST_ASSERT_EQUAL_UINT64(3'300, deadline);
  end();
}

static void test_session_timeout_is_earliest_mapping() {
  begin(100);
  map();
  map();

  // one event per call, so each mapping's send can happen at a different time
  TEST_ASSERT_EQUAL(PCP_EVENT_SEND, pcp_session_poll(session, 500).type);
  TEST_ASSERT_EQUAL(PCP_EVENT_SEND, pcp_session_poll(session, 200).type);

  auto const event = pcp_session_poll(session, 200);
  TEST_ASSERT_EQUAL(PCP_EVENT_TIMEOUT, event.type);
  TEST_ASSERT_TRUE(event.timeout.deadline.has_value);
  TEST_ASSERT_EQUAL_UINT64(3'200, event.timeout.deadline.value);
  end();
}

static void write_uint32(uint8_t dst[4], uint32_t value) {
  dst[0] = (uint8_t)(value >> 24);
  dst[1] = (uint8_t)(value >> 16);
  dst[2] = (uint8_t)(value >> 8);
  dst[3] = (uint8_t)value;
}

static ip_address_t const external_ip = {
    .addr = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 203, 0, 113, 7}};
static constexpr port_t external_port = 54321;

// delivers a well-formed response with the given epoch time. With a mapping it
// answers that mapping's request (with `peer` replacing its remote peer if
// given), otherwise its body doesn't match any mapping
static void deliver(pcp_time_ms_t now,
                    pcp_time_s_t epoch,
                    pcp_mapping_t const *mapping,
                    pcp_result_t result,
                    pcp_time_s_t lifetime,
                    socket_address_t const *peer) {
  uint8_t data[80] = {0x02, 0x81, 0x00, result};
  size_t size = 60;
  write_uint32(data + 4, lifetime);
  write_uint32(data + 8, epoch);

  if (mapping) {
    auto const request = &mapping->request;
    data[1] = (uint8_t)(0x80 | request->type);
    memcpy(data + 24, mapping->nonce.data, sizeof(mapping->nonce.data));
    data[36] = request->protocol;
    data[40] = (uint8_t)(request->port >> 8);
    data[41] = (uint8_t)request->port;

    if (request->type == PEER) {
      auto const remote = peer ? peer : &request->peer.peer;
      data[60] = (uint8_t)(remote->port >> 8);
      data[61] = (uint8_t)remote->port;
      memcpy(data + 64, remote->ip.addr, sizeof(remote->ip.addr));
      size = 80;
    }
  }

  data[42] = (uint8_t)(external_port >> 8);
  data[43] = (uint8_t)external_port;
  memcpy(data + 44, external_ip.addr, sizeof(external_ip.addr));

  pcp_session_on_datagram(session, now, size, data);
}

// a response that doesn't belong to any mapping
static void receive_response(pcp_time_ms_t now, pcp_time_s_t epoch) {
  deliver(now, epoch, nullptr, SUCCESS, 0, nullptr);
}

// the server's answer to `handle`'s request, with an epoch time that stays
// consistent with `now`
static void respond(pcp_handle_t handle,
                    pcp_time_ms_t now,
                    pcp_result_t result,
                    pcp_time_s_t lifetime) {
  deliver(now, (pcp_time_s_t)(1'000 + now / 1'000),
          (pcp_mapping_t const *)handle, result, lifetime, nullptr);
}

// polls the event the application should get next
static pcp_event_t next_event(pcp_time_ms_t now) {
  return pcp_session_poll(session, now);
}

static void test_session_server_reset_requests_again() {
  begin(100);
  map();

  // first send at 0, then backing off: next retransmissions at 3s and 9s
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));
  TEST_ASSERT_EQUAL_size_t(1, send_due(3'000));
  TEST_ASSERT_EQUAL_UINT64(9'000, deadline);

  // consistent epochs leave the schedule alone
  receive_response(4'000, 1'000);
  receive_response(5'000, 1'001);
  TEST_ASSERT_EQUAL_size_t(0, send_due(5'000));
  TEST_ASSERT_EQUAL_UINT64(9'000, deadline);

  // the server restarted: request again right away, backoff starting over
  receive_response(6'000, 3);
  TEST_ASSERT_EQUAL_size_t(1, send_due(6'000));
  TEST_ASSERT_EQUAL_UINT64(9'000, deadline);
  TEST_ASSERT_EQUAL_size_t(1, send_due(9'000));
  TEST_ASSERT_EQUAL_UINT64(15'000, deadline);
  end();
}

// gets `handle` mapped by the server and checks the application learns that
static void establish(pcp_handle_t handle, pcp_time_ms_t now) {
  TEST_ASSERT_EQUAL_size_t(1, send_due(now));
  respond(handle, now, SUCCESS, 3600);

  auto const event = next_event(now);
  TEST_ASSERT_EQUAL(PCP_EVENT_MAPPED, event.type);
  TEST_ASSERT_EQUAL_UINT64(handle, event.mapped.handle);
}

static void test_session_success_maps() {
  begin(100);
  auto const handle = map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));

  respond(handle, 500, SUCCESS, 3600);

  auto const event = next_event(500);
  TEST_ASSERT_EQUAL(PCP_EVENT_MAPPED, event.type);
  TEST_ASSERT_EQUAL_UINT64(handle, event.mapped.handle);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(external_ip.addr, event.mapped.external.ip.addr,
                                16);
  TEST_ASSERT_EQUAL_UINT16(external_port, event.mapped.external.port);

  // no more retransmissions, just the lifetime running out
  TEST_ASSERT_EQUAL_size_t(0, send_due(500));
  TEST_ASSERT_EQUAL_UINT64(500 + 3'600'000, deadline);
  end();
}

static void test_session_mapping_expires() {
  begin(100);
  auto const handle = map();
  establish(handle, 0);

  TEST_ASSERT_EQUAL_size_t(0, send_due(3'599'999));

  // the application learns its address is gone, and it's requested again
  auto const event = next_event(3'600'000);
  TEST_ASSERT_EQUAL(PCP_EVENT_UNMAPPED, event.type);
  TEST_ASSERT_EQUAL_UINT64(handle, event.unmapped.handle);
  TEST_ASSERT_EQUAL_size_t(1, send_due(3'600'000));
  TEST_ASSERT_EQUAL_UINT64(3'603'000, deadline);
  end();
}

static void test_session_temporary_error_retries_later() {
  begin(100);
  auto const handle = map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));
  TEST_ASSERT_EQUAL_UINT64(3'000, deadline);

  // the server asks us to wait 30s, which the application doesn't hear about
  respond(handle, 500, NO_RESOURCES, 30);
  TEST_ASSERT_EQUAL_size_t(0, send_due(500));
  TEST_ASSERT_EQUAL_UINT64(30'500, deadline);
  TEST_ASSERT_EQUAL_size_t(0, send_due(30'499));

  // then a fresh backoff
  TEST_ASSERT_EQUAL_size_t(1, send_due(30'500));
  TEST_ASSERT_EQUAL_UINT64(33'500, deadline);
  end();
}

static void test_session_permanent_error_fails() {
  begin(100);
  auto const handle = map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));

  respond(handle, 500, NOT_AUTHORIZED, 0);

  auto event = next_event(500);
  TEST_ASSERT_EQUAL(PCP_EVENT_FAILED, event.type);
  TEST_ASSERT_EQUAL_UINT64(handle, event.failed.handle);
  TEST_ASSERT_EQUAL(NOT_AUTHORIZED, event.failed.result);

  // nothing left to do for it, ever
  event = next_event(500);
  TEST_ASSERT_EQUAL(PCP_EVENT_TIMEOUT, event.type);
  TEST_ASSERT_FALSE(event.timeout.deadline.has_value);
  end();
}

static void test_session_unknown_result_fails() {
  begin(100);
  auto const handle = map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));

  respond(handle, 500, (pcp_result_t)200, 0);

  auto const event = next_event(500);
  TEST_ASSERT_EQUAL(PCP_EVENT_FAILED, event.type);
  TEST_ASSERT_EQUAL(200, event.failed.result);
  end();
}

static void test_session_response_matches_its_mapping_only() {
  begin(100);
  auto const first = map();
  rng_value = 101; // a different nonce for the second mapping
  auto const second = map();
  rng_value = 100; // and no jitter again
  TEST_ASSERT_EQUAL_size_t(2, send_due(0));

  respond(second, 500, SUCCESS, 3600);

  auto const event = next_event(500);
  TEST_ASSERT_EQUAL(PCP_EVENT_MAPPED, event.type);
  TEST_ASSERT_EQUAL_UINT64(second, event.mapped.handle);

  // the first one is still being requested
  TEST_ASSERT_EQUAL_size_t(0, send_due(500));
  TEST_ASSERT_EQUAL_UINT64(3'000, deadline);
  TEST_ASSERT_EQUAL_size_t(1, send_due(3'000));

  respond(first, 3'500, SUCCESS, 3600);
  TEST_ASSERT_EQUAL_UINT64(first, next_event(3'500).mapped.handle);
  end();
}

static void test_session_peer_response_must_match_remote_peer() {
  begin(100);
  socket_address_t const remote = {.ip =
                                       ip_address_from_octets(198, 51, 100, 1),
                                   .port = 443};
  auto const handle =
      pcp_session_peer_request(session, 3600, TCP, 1234, remote, 0, nullptr);
  TEST_ASSERT_TRUE(handle.has_value);
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));

  // same nonce, protocol and port, but a different remote peer
  socket_address_t const other = {.ip = remote.ip, .port = 444};
  deliver(500, 1'000, (pcp_mapping_t const *)handle.value, SUCCESS, 3600,
          &other);
  TEST_ASSERT_EQUAL_size_t(0, send_due(500));

  deliver(600, 1'000, (pcp_mapping_t const *)handle.value, SUCCESS, 3600,
          nullptr);
  TEST_ASSERT_EQUAL(PCP_EVENT_MAPPED, next_event(600).type);
  end();
}

static void test_session_duplicate_response_ignored() {
  begin(100);
  auto const handle = map();
  establish(handle, 0);

  // e.g. the answer to a retransmission of the same request
  respond(handle, 1'000, SUCCESS, 3600);
  TEST_ASSERT_EQUAL(PCP_EVENT_TIMEOUT, next_event(1'000).type);
  end();
}

static void test_session_server_reset_unmaps_active_mapping() {
  begin(100);
  auto const handle = map();
  establish(handle, 0);

  receive_response(1'000, 1'000);
  receive_response(2'000, 3);

  // first the application learns the address is gone, then it's requested again
  auto event = next_event(2'000);
  TEST_ASSERT_EQUAL(PCP_EVENT_UNMAPPED, event.type);
  TEST_ASSERT_EQUAL_UINT64(handle, event.unmapped.handle);

  event = next_event(2'000);
  TEST_ASSERT_EQUAL(PCP_EVENT_SEND, event.type);

  TEST_ASSERT_EQUAL_size_t(0, send_due(2'000));
  TEST_ASSERT_EQUAL_UINT64(5'000, deadline);
  end();
}

static void test_session_server_reset_unmaps_once() {
  begin(100);
  auto const handle = map();
  establish(handle, 0);

  // two resets before the application polls still mean one lost address
  receive_response(1'000, 1'000);
  receive_response(2'000, 3);
  receive_response(3'000, 1'000);
  receive_response(4'000, 3);

  TEST_ASSERT_EQUAL(PCP_EVENT_UNMAPPED, next_event(4'000).type);
  TEST_ASSERT_EQUAL_size_t(1, send_due(4'000));
  end();
}

static void test_session_server_reset_pending_mapping_not_unmapped() {
  begin(100);
  map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));

  receive_response(1'000, 1'000);
  receive_response(2'000, 3);

  // it never had an address, so it's just requested again
  TEST_ASSERT_EQUAL_size_t(1, send_due(2'000));
  end();
}

static void test_session_server_reset_before_mapped_event() {
  begin(100);
  auto const handle = map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));

  // granted, but the server restarts before the application polls
  respond(handle, 500, SUCCESS, 3600);
  receive_response(1'000, 3);

  // the application never saw the address, so neither MAPPED nor UNMAPPED
  TEST_ASSERT_EQUAL_size_t(1, send_due(1'000));
  end();
}

static void test_session_response_revealing_reset_not_applied() {
  begin(100);
  auto const handle = map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));
  receive_response(500, 1'000);

  // matches the request, but its epoch says the server lost its state, so it
  // might as well be stale
  deliver(1'000, 3, (pcp_mapping_t const *)handle, SUCCESS, 3600, nullptr);
  TEST_ASSERT_EQUAL_size_t(1, send_due(1'000));
  end();
}

static void test_session_ignores_malformed_datagrams() {
  begin(100);
  map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));

  receive_response(1'000, 1'000);
  uint8_t garbage[60] = {0x02, 0x01}; // a request, not a response
  pcp_session_on_datagram(session, 2'000, sizeof(garbage), garbage);
  TEST_ASSERT_EQUAL_size_t(0, send_due(2'000));
  TEST_ASSERT_EQUAL_UINT64(3'000, deadline);
  end();
}

// simulates an application loop that wakes exactly at each deadline, which
// send_due checks always lies in the future
static void test_session_wakes_at_deadlines() {
  begin(0); // jitter -10%, shrinking intervals the most
  map();
  map();
  map();

  pcp_time_ms_t now = 0;
  for (int i = 0; i < 50; ++i) {
    TEST_ASSERT_GREATER_THAN_size_t(0, send_due(now));
    now = deadline;
  }
  end();
}

void session_tests() {
  RUN_TEST(test_session_never_without_mappings);
  RUN_TEST(test_session_new_request_due_immediately);
  RUN_TEST(test_session_no_send_before_due);
  RUN_TEST(test_session_send_writes_into_config_buffer);
  RUN_TEST(test_session_retransmits_on_schedule);
  RUN_TEST(test_session_jitter_applied);
  RUN_TEST(test_session_timeout_is_earliest_mapping);
  RUN_TEST(test_session_wakes_at_deadlines);
  RUN_TEST(test_session_server_reset_requests_again);
  RUN_TEST(test_session_success_maps);
  RUN_TEST(test_session_mapping_expires);
  RUN_TEST(test_session_temporary_error_retries_later);
  RUN_TEST(test_session_permanent_error_fails);
  RUN_TEST(test_session_unknown_result_fails);
  RUN_TEST(test_session_response_matches_its_mapping_only);
  RUN_TEST(test_session_peer_response_must_match_remote_peer);
  RUN_TEST(test_session_duplicate_response_ignored);
  RUN_TEST(test_session_server_reset_unmaps_active_mapping);
  RUN_TEST(test_session_server_reset_unmaps_once);
  RUN_TEST(test_session_server_reset_pending_mapping_not_unmapped);
  RUN_TEST(test_session_server_reset_before_mapped_event);
  RUN_TEST(test_session_response_revealing_reset_not_applied);
  RUN_TEST(test_session_ignores_malformed_datagrams);
}
