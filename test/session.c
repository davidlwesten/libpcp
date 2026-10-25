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

// delivers a well-formed MAP response with the given epoch time, whose body
// doesn't match any mapping
static void receive_response(pcp_time_ms_t now, pcp_time_s_t epoch) {
  uint8_t data[60] = {0x02, 0x81};
  data[8] = (uint8_t)(epoch >> 24);
  data[9] = (uint8_t)(epoch >> 16);
  data[10] = (uint8_t)(epoch >> 8);
  data[11] = (uint8_t)epoch;
  pcp_session_on_datagram(session, now, sizeof(data), data);
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

// stands in for a successful response until responses are matched to mappings
static void activate(pcp_handle_t handle) {
  ((pcp_mapping_t *)handle)->state = (pcp_mapping_state_t){.type = ACTIVE};
}

static void test_session_server_reset_unmaps_active_mapping() {
  begin(100);
  auto const handle = map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));
  activate(handle);

  receive_response(1'000, 1'000);
  receive_response(2'000, 3);

  // first the application learns the address is gone, then it's requested again
  auto event = pcp_session_poll(session, 2'000);
  TEST_ASSERT_EQUAL(PCP_EVENT_UNMAPPED, event.type);
  TEST_ASSERT_EQUAL_UINT64(handle, event.unmapped.handle);

  event = pcp_session_poll(session, 2'000);
  TEST_ASSERT_EQUAL(PCP_EVENT_SEND, event.type);

  TEST_ASSERT_EQUAL_size_t(0, send_due(2'000));
  TEST_ASSERT_EQUAL_UINT64(5'000, deadline);
  end();
}

static void test_session_server_reset_unmaps_once() {
  begin(100);
  auto const handle = map();
  TEST_ASSERT_EQUAL_size_t(1, send_due(0));
  activate(handle);

  // two resets before the application polls still mean one lost address
  receive_response(1'000, 1'000);
  receive_response(2'000, 3);
  receive_response(3'000, 1'000);
  receive_response(4'000, 3);

  TEST_ASSERT_EQUAL(PCP_EVENT_UNMAPPED, pcp_session_poll(session, 4'000).type);
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
  RUN_TEST(test_session_server_reset_unmaps_active_mapping);
  RUN_TEST(test_session_server_reset_unmaps_once);
  RUN_TEST(test_session_server_reset_pending_mapping_not_unmapped);
  RUN_TEST(test_session_ignores_malformed_datagrams);
}
