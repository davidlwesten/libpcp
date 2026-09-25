#include <pcp/pcp.h>

#include "client.h"
#include "lease.h"
#include "test_transport.h"

#include <assert.h>
#include <string.h>
#include <unity.h>

static pcp_lease_t *first_lease(pcp_client_t *client) {
  pcp_client_internal_t *internal = client->data;
  assert(internal->leases[0].has_value);
  return &internal->leases[0].value;
}

static size_t active_timer_count(test_transport_t const *transport) {
  size_t count = 0;
  for (size_t i = 0; i < TEST_TRANSPORT_MAX_TIMERS; ++i) {
    if (transport->timers[i].active) {
      ++count;
    }
  }
  return count;
}

static void assert_last_outbound_is_delete(test_transport_t const *transport) {
  TEST_ASSERT_TRUE(transport->outbound_count > 0);
  size_t const index = transport->outbound_count - 1;
  TEST_ASSERT_EQUAL_UINT8(2, transport->outbound_packets[index][0]);
  TEST_ASSERT_EQUAL_UINT8(1, transport->outbound_packets[index][1]);
  TEST_ASSERT_EQUAL_UINT8(0, transport->outbound_packets[index][4]);
  TEST_ASSERT_EQUAL_UINT8(0, transport->outbound_packets[index][5]);
  TEST_ASSERT_EQUAL_UINT8(0, transport->outbound_packets[index][6]);
  TEST_ASSERT_EQUAL_UINT8(0, transport->outbound_packets[index][7]);
}

static bool advance_to_next_timer(test_transport_t *transport) {
  test_transport_timer_t const *next = nullptr;
  for (size_t i = 0; i < TEST_TRANSPORT_MAX_TIMERS; ++i) {
    test_transport_timer_t const *timer = &transport->timers[i];
    if (timer->active && (!next || timer->expiration < next->expiration)) {
      next = timer;
    }
  }

  if (!next) {
    return false;
  }

  double const delay = next->expiration - transport->now;
  test_transport_advance(transport, delay > 0.0 ? delay : 0.0);
  return true;
}

static void enqueue_map_response(test_transport_t *transport,
                                 pcp_result_t code,
                                 uint32_t ttl) {
  uint8_t response[60] = {0};
  response[0] = 2;
  response[1] = 0x80 | 1;
  response[3] = (uint8_t)code;
  response[4] = (uint8_t)(ttl >> 24);
  response[5] = (uint8_t)(ttl >> 16);
  response[6] = (uint8_t)(ttl >> 8);
  response[7] = (uint8_t)ttl;
  response[11] = 1;
  response[36] = UDP;
  response[40] = (uint8_t)(12345 >> 8);
  response[41] = (uint8_t)12345;
  response[42] = (uint8_t)(54321 >> 8);
  response[43] = (uint8_t)54321;
  ip_address_t const external_ip = ip_address_from_octets(203, 0, 113, 9);
  memcpy(response + 44, external_ip.addr, sizeof(external_ip.addr));

  test_transport_enqueue_inbound(
      transport,
      (pcp_transport_const_view_t){.size = sizeof(response), .data = response});
}

static pcp_client_t create_map_client(test_transport_t *transport) {
  pcp_transport_t const interface = test_transport_interface(transport);
  pcp_client_result_t const result = pcp_client_create(interface);
  assert(result.has_value);
  pcp_client_t client = result.value;
  socket_address_t const external = {.ip = ip_address_any_v6, .port = 12345};
  CALL(&client, map, UDP, 12345, external, 60, 0, nullptr);
  return client;
}

static void test_pending_activation_and_renewal(void) {
  socket_address_t const local = {.ip = ip_address_loopback_v4, .port = 12345};
  socket_address_t const remote = {.ip = ip_address_loopback_v4, .port = 5351};
  test_transport_result_t transport_result =
      test_transport_create(local, remote);
  TEST_ASSERT_TRUE(transport_result.has_value);
  test_transport_t *transport = &transport_result.value;
  pcp_client_t client = create_map_client(transport);

  TEST_ASSERT_EQUAL_INT(PENDING, first_lease(&client)->state);
  TEST_ASSERT_EQUAL_UINT32(1, transport->outbound_count);
  TEST_ASSERT_EQUAL_UINT32(1, active_timer_count(transport));

  TEST_ASSERT_TRUE(advance_to_next_timer(transport));
  TEST_ASSERT_EQUAL_INT(PENDING, first_lease(&client)->state);
  TEST_ASSERT_EQUAL_UINT32(2, transport->outbound_count);

  enqueue_map_response(transport, SUCCESS, 60);
  test_transport_deliver_next(transport, client.data);
  TEST_ASSERT_EQUAL_INT(ACTIVE, first_lease(&client)->state);
  TEST_ASSERT_EQUAL_UINT32(1, active_timer_count(transport));

  TEST_ASSERT_TRUE(advance_to_next_timer(transport));
  TEST_ASSERT_EQUAL_INT(RENEWING, first_lease(&client)->state);
  TEST_ASSERT_EQUAL_UINT32(3, transport->outbound_count);

  enqueue_map_response(transport, SUCCESS, 60);
  test_transport_deliver_next(transport, client.data);
  TEST_ASSERT_EQUAL_INT(ACTIVE, first_lease(&client)->state);
  TEST_ASSERT_EQUAL_UINT32(1, active_timer_count(transport));

  pcp_client_destroy(&client);
  TEST_ASSERT_EQUAL_UINT32(0, active_timer_count(transport));
  TEST_ASSERT_EQUAL_UINT32(4, transport->outbound_count);
  assert_last_outbound_is_delete(transport);
}

static void test_destroying_renewing_lease_deletes_mapping(void) {
  socket_address_t const local = {.ip = ip_address_loopback_v4, .port = 12345};
  socket_address_t const remote = {.ip = ip_address_loopback_v4, .port = 5351};
  test_transport_result_t transport_result =
      test_transport_create(local, remote);
  TEST_ASSERT_TRUE(transport_result.has_value);
  test_transport_t *transport = &transport_result.value;
  pcp_client_t client = create_map_client(transport);

  enqueue_map_response(transport, SUCCESS, 60);
  test_transport_deliver_next(transport, client.data);
  TEST_ASSERT_EQUAL_INT(ACTIVE, first_lease(&client)->state);
  TEST_ASSERT_TRUE(advance_to_next_timer(transport));
  TEST_ASSERT_EQUAL_INT(RENEWING, first_lease(&client)->state);
  TEST_ASSERT_EQUAL_UINT32(2, transport->outbound_count);

  pcp_client_destroy(&client);
  TEST_ASSERT_EQUAL_UINT32(0, active_timer_count(transport));
  TEST_ASSERT_EQUAL_UINT32(3, transport->outbound_count);
  assert_last_outbound_is_delete(transport);
}

static void test_transient_and_permanent_response_errors(void) {
  socket_address_t const local = {.ip = ip_address_loopback_v4, .port = 12345};
  socket_address_t const remote = {.ip = ip_address_loopback_v4, .port = 5351};
  test_transport_result_t transport_result =
      test_transport_create(local, remote);
  TEST_ASSERT_TRUE(transport_result.has_value);
  test_transport_t *transport = &transport_result.value;
  pcp_client_t client = create_map_client(transport);

  enqueue_map_response(transport, NETWORK_FAILURE, 2);
  test_transport_deliver_next(transport, client.data);
  TEST_ASSERT_EQUAL_INT(PENDING, first_lease(&client)->state);
  TEST_ASSERT_EQUAL_UINT32(1, transport->outbound_count);
  TEST_ASSERT_EQUAL_UINT32(1, active_timer_count(transport));

  TEST_ASSERT_TRUE(advance_to_next_timer(transport));
  TEST_ASSERT_EQUAL_INT(PENDING, first_lease(&client)->state);
  TEST_ASSERT_EQUAL_UINT32(2, transport->outbound_count);

  enqueue_map_response(transport, NOT_AUTHORIZED, 0);
  test_transport_deliver_next(transport, client.data);
  TEST_ASSERT_EQUAL_INT(FAILED, first_lease(&client)->state);
  TEST_ASSERT_EQUAL_UINT32(0, active_timer_count(transport));

  test_transport_advance(transport, 100.0);
  TEST_ASSERT_EQUAL_UINT32(2, transport->outbound_count);
  TEST_ASSERT_EQUAL_INT(FAILED, first_lease(&client)->state);

  pcp_client_destroy(&client);
}

void lease_tests(void) {
  RUN_TEST(test_pending_activation_and_renewal);
  RUN_TEST(test_destroying_renewing_lease_deletes_mapping);
  RUN_TEST(test_transient_and_permanent_response_errors);
}
