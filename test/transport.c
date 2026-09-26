#include "transport.h"

#include <pcp/pcp.h>

#include <assert.h>
#include <string.h>
#include <unity.h>

#include "runner.h"

static socket_address_t test_transport_local(test_transport_t *self) {
  return self->local;
}

static socket_address_t test_transport_remote(test_transport_t *self) {
  return self->remote;
}

static void test_transport_stop(test_transport_t *self) {
  self->stopped = true;
}

static void test_transport_close(test_transport_t *self) {
  self->closed = true;
}

static void test_transport_fire_due(test_transport_t *self) {
  while (true) {
    test_transport_timer_t *next = nullptr;
    for (size_t i = 0; i < TEST_TRANSPORT_MAX_TIMERS; ++i) {
      test_transport_timer_t *timer = &self->timers[i];
      if (!timer->active) {
        continue;
      }
      if (!next || timer->expiration < next->expiration) {
        next = timer;
      }
    }

    if (!next || next->expiration > self->now) {
      break;
    }

    pcp_transport_on_timer_t action = next->action;
    next->active = false;
    if (action.fn) {
      action.fn(action.userdata);
    }
  }
}

static void test_transport_send(test_transport_t *self,
                                pcp_transport_const_view_t packet,
                                pcp_transport_on_send_t callback) {
  assert(self);

  if (self->outbound_count >= TEST_TRANSPORT_MAX_PACKETS) {
    if (callback.fn) {
      callback.fn(
          callback.userdata,
          (pcp_transport_size_or_error_t){.has_value = false, .error = ENOSPC});
    }
    return;
  }

  assert(packet.size <= TEST_TRANSPORT_MAX_PACKET_SIZE);
  uint8_t *dst = self->outbound_packets[self->outbound_count];
  memcpy(dst, packet.data, packet.size);
  self->outbound_sizes[self->outbound_count] = packet.size;
  ++self->outbound_count;

  if (callback.fn) {
    callback.fn(callback.userdata,
                (pcp_transport_size_or_error_t){.has_value = true,
                                                .value = packet.size});
  }
}

static pcp_transport_timer_result_t
test_transport_add_timer(test_transport_t *self,
                         pcp_transport_timeout_t delay,
                         pcp_transport_on_timer_t action) {
  assert(self);
  assert(delay.seconds > 0.0);

  for (size_t i = 0; i < TEST_TRANSPORT_MAX_TIMERS; ++i) {
    test_transport_timer_t *timer = &self->timers[i];
    if (!timer->active) {
      *timer = (test_transport_timer_t){.active = true,
                                        .id = self->next_timer_id++,
                                        .expiration = self->now + delay.seconds,
                                        .action = action};
      return (pcp_transport_timer_result_t){.has_value = true,
                                            .value = timer->id};
    }
  }

  return (pcp_transport_timer_result_t){.has_value = false, .error = ENOSPC};
}

static void test_transport_remove_timer(test_transport_t *self,
                                        pcp_transport_timer_t id) {
  assert(self);
  for (size_t i = 0; i < TEST_TRANSPORT_MAX_TIMERS; ++i) {
    test_transport_timer_t *timer = &self->timers[i];
    if (timer->active && timer->id == id) {
      timer->active = false;
      return;
    }
  }
}

test_transport_result_t test_transport_create(socket_address_t local,
                                              socket_address_t remote) {
  test_transport_t transport = {.local = local,
                                .remote = remote,
                                .outbound_count = 0,
                                .inbound_count = 0,
                                .timers = {},
                                .next_timer_id = 1,
                                .now = 0.0,
                                .stopped = false,
                                .closed = false};

  return (test_transport_result_t){.has_value = true, .value = transport};
}

pcp_transport_t test_transport_interface(test_transport_t *transport) {
  assert(transport);
  return (pcp_transport_t){
      .data = transport,
      .local = (pcp_transport_endpoint_t)test_transport_local,
      .remote = (pcp_transport_endpoint_t)test_transport_remote,
      .send = (pcp_transport_send_t)test_transport_send,
      .add_timer = (pcp_transport_add_timer_t)test_transport_add_timer,
      .remove_timer = (pcp_transport_remove_timer_t)test_transport_remove_timer,
      .stop = (pcp_transport_stop_t)test_transport_stop,
      .close = (pcp_transport_close_t)test_transport_close};
}

void test_transport_advance(test_transport_t *transport, double seconds) {
  assert(transport);
  transport->now += seconds;
  test_transport_fire_due(transport);
}

void test_transport_enqueue_inbound(test_transport_t *transport,
                                    pcp_transport_const_view_t packet) {
  assert(transport);
  assert(packet.size <= TEST_TRANSPORT_MAX_PACKET_SIZE);
  assert(transport->inbound_count < TEST_TRANSPORT_MAX_PACKETS);

  uint8_t *dst = transport->inbound_packets[transport->inbound_count];
  memcpy(dst, packet.data, packet.size);
  transport->inbound_sizes[transport->inbound_count] = packet.size;
  ++transport->inbound_count;
}

bool test_transport_take_inbound(test_transport_t *transport,
                                 pcp_transport_const_view_t *packet) {
  assert(transport);
  assert(packet);

  if (transport->inbound_count == 0) {
    return false;
  }

  size_t const idx = transport->inbound_count - 1;
  packet->size = transport->inbound_sizes[idx];
  packet->data = transport->inbound_packets[idx];
  --transport->inbound_count;
  return true;
}

bool test_transport_take_outbound(test_transport_t *transport,
                                  pcp_transport_const_view_t *packet) {
  assert(transport);
  assert(packet);

  if (transport->outbound_count == 0) {
    return false;
  }

  size_t const idx = transport->outbound_count - 1;
  packet->size = transport->outbound_sizes[idx];
  packet->data = transport->outbound_packets[idx];
  --transport->outbound_count;
  return true;
}

void test_transport_deliver_next(test_transport_t *transport, void *client) {
  assert(transport);
  assert(client);

  pcp_transport_const_view_t packet = {};
  if (!test_transport_take_inbound(transport, &packet)) {
    return;
  }

  pcp_client_recv(client, packet);
}

static void timer_callback(void *userdata) {
  bool *flag = (bool *)userdata;
  *flag = true;
}

static void test_test_transport_send_and_receive(void) {
  const socket_address_t local = {.ip = ip_address_loopback_v4, .port = 1234};
  const socket_address_t remote = {.ip = ip_address_loopback_v4, .port = 4321};
  test_transport_result_t result = test_transport_create(local, remote);
  TEST_ASSERT_TRUE(result.has_value);

  test_transport_t *transport = &result.value;
  pcp_transport_t iface = test_transport_interface(transport);

  const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
  pcp_transport_const_view_t packet = {.size = sizeof(payload),
                                       .data = payload};

  iface.send(iface.data, packet,
             (pcp_transport_on_send_t){.fn = nullptr, .userdata = nullptr});

  pcp_transport_const_view_t outbound = {};
  TEST_ASSERT_TRUE(test_transport_take_outbound(transport, &outbound));
  TEST_ASSERT_EQUAL_UINT32(sizeof(payload), outbound.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, outbound.data, sizeof(payload));

  test_transport_enqueue_inbound(transport, packet);
  TEST_ASSERT_TRUE(test_transport_take_inbound(transport, &outbound));
  TEST_ASSERT_EQUAL_UINT32(sizeof(payload), outbound.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, outbound.data, sizeof(payload));
}

static void test_test_transport_timers(void) {
  const socket_address_t local = {.ip = ip_address_loopback_v4, .port = 1234};
  const socket_address_t remote = {.ip = ip_address_loopback_v4, .port = 4321};
  test_transport_result_t result = test_transport_create(local, remote);
  TEST_ASSERT_TRUE(result.has_value);

  test_transport_t *transport = &result.value;
  pcp_transport_t iface = test_transport_interface(transport);

  bool fired = false;
  pcp_transport_timer_result_t timer = iface.add_timer(
      iface.data, (pcp_transport_timeout_t){.seconds = 1.0},
      (pcp_transport_on_timer_t){.fn = timer_callback, .userdata = &fired});

  TEST_ASSERT_TRUE(timer.has_value);
  test_transport_advance(transport, 0.5);
  TEST_ASSERT_FALSE(fired);
  test_transport_advance(transport, 0.6);
  TEST_ASSERT_TRUE(fired);

  pcp_transport_const_view_t packet = {};
  TEST_ASSERT_FALSE(test_transport_take_outbound(transport, &packet));
}

void test_transport_tests(void) {
  RUN_TEST(test_test_transport_send_and_receive);
  RUN_TEST(test_test_transport_timers);
}
