#pragma once

#include <pcp/transport.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

constexpr size_t TEST_TRANSPORT_MAX_TIMERS = 16;
constexpr size_t TEST_TRANSPORT_MAX_PACKETS = 32;
constexpr size_t TEST_TRANSPORT_MAX_PACKET_SIZE = 1100;

typedef struct test_transport_timer {
  bool active;
  uint32_t id;
  double expiration;
  pcp_transport_on_timer_t action;
} test_transport_timer_t;

typedef struct test_transport {
  socket_address_t local;
  socket_address_t remote;
  uint8_t outbound_packets[TEST_TRANSPORT_MAX_PACKETS]
                          [TEST_TRANSPORT_MAX_PACKET_SIZE];
  size_t outbound_sizes[TEST_TRANSPORT_MAX_PACKETS];
  size_t outbound_count;
  uint8_t inbound_packets[TEST_TRANSPORT_MAX_PACKETS]
                         [TEST_TRANSPORT_MAX_PACKET_SIZE];
  size_t inbound_sizes[TEST_TRANSPORT_MAX_PACKETS];
  size_t inbound_count;
  pcp_transport_on_send_t pending_send_callbacks[TEST_TRANSPORT_MAX_PACKETS];
  size_t pending_send_callback_count;
  bool defer_send_callbacks;
  test_transport_timer_t timers[TEST_TRANSPORT_MAX_TIMERS];
  uint32_t next_timer_id;
  double now;
  bool stopped;
  bool closed;
} test_transport_t;

typedef struct test_transport_result {
  bool has_value;
  union {
    test_transport_t value;
    errno_t error;
  };
} test_transport_result_t;

test_transport_result_t test_transport_create(socket_address_t local,
                                              socket_address_t remote);
pcp_transport_t test_transport_interface(test_transport_t *transport);
void test_transport_advance(test_transport_t *transport, double seconds);
bool test_transport_complete_next_send(test_transport_t *transport);
bool test_transport_complete_next_send_with_result(
    test_transport_t *transport,
    pcp_transport_size_or_error_t result);
void test_transport_enqueue_inbound(test_transport_t *transport,
                                    pcp_transport_const_view_t packet);
bool test_transport_take_inbound(test_transport_t *transport,
                                 pcp_transport_const_view_t *packet);
bool test_transport_take_outbound(test_transport_t *transport,
                                  pcp_transport_const_view_t *packet);
void test_transport_deliver_next(test_transport_t *transport, void *client);
