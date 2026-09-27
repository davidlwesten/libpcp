#include <pcp/pcp.h>

#include "transport.h"

#include <stdint.h>
#include <stdlib.h>
#include <unity.h>

#include "runner.h"

typedef struct allocator_state {
  size_t allocations;
  size_t frees;
  bool fail_alloc;
} allocator_state_t;

static void *test_allocator_alloc(void *ctx, size_t size) {
  allocator_state_t *state = ctx;
  ++state->allocations;
  if (state->fail_alloc) {
    return nullptr;
  }
  return malloc(size);
}

static void test_allocator_free(void *ctx, void *ptr) {
  allocator_state_t *state = ctx;
  ++state->frees;
  free(ptr);
}

static void test_custom_allocator_lifecycle() {
  socket_address_t const local = {.ip = ip_address_loopback_v4, .port = 12345};
  socket_address_t const remote = {.ip = ip_address_loopback_v4, .port = 5351};
  test_transport_result_t transport_result =
      test_transport_create(local, remote);
  TEST_ASSERT_TRUE(transport_result.has_value);

  allocator_state_t state = {.fail_alloc = true};
  pcp_allocator_t const allocator = {.alloc = test_allocator_alloc,
                                     .free = test_allocator_free,
                                     .ctx = &state};
  pcp_client_result_t result = pcp_client_create(
      test_transport_interface(&transport_result.value), allocator);
  TEST_ASSERT_FALSE(result.has_value);
  TEST_ASSERT_EQUAL_INT(ENOMEM, result.error);

  state.fail_alloc = false;
  result = pcp_client_create(test_transport_interface(&transport_result.value),
                             allocator);
  TEST_ASSERT_TRUE(result.has_value);
  TEST_ASSERT_EQUAL_UINT32(2, state.allocations);

  pcp_client_t client = result.value;
  socket_address_t const external = {.ip = ip_address_any_v6, .port = 12345};
  CALL(&client, map, UDP, 12345, external, 60, 0, nullptr);
  TEST_ASSERT_EQUAL_UINT32(3, state.allocations);

  pcp_client_destroy(&client);
  TEST_ASSERT_EQUAL_UINT32(2, state.frees);
}

static void test_arena_allocator_alignment_and_exhaustion() {
  unsigned char too_small[1];
  pcp_allocator_t const invalid_allocator =
      pcp_allocator_arena(too_small, sizeof(too_small));
  TEST_ASSERT_NULL(invalid_allocator.alloc(invalid_allocator.ctx, 1));

  unsigned char storage[64 + alignof(max_align_t)];
  pcp_allocator_t const allocator =
      pcp_allocator_arena(storage + 1, sizeof(storage) - 1);

  void *first = allocator.alloc(allocator.ctx, 1);
  void *second = allocator.alloc(allocator.ctx, sizeof(max_align_t));
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_UINT32(0, (uintptr_t)first % alignof(max_align_t));
  TEST_ASSERT_EQUAL_UINT32(0, (uintptr_t)second % alignof(max_align_t));
  TEST_ASSERT_NULL(allocator.alloc(allocator.ctx, sizeof(storage)));

  allocator.free(allocator.ctx, first);
  TEST_ASSERT_NULL(allocator.alloc(allocator.ctx, sizeof(storage)));
}

static void test_arena_allocator_client_lifecycle() {
  socket_address_t const local = {.ip = ip_address_loopback_v4, .port = 12345};
  socket_address_t const remote = {.ip = ip_address_loopback_v4, .port = 5351};
  test_transport_result_t transport_result =
      test_transport_create(local, remote);
  TEST_ASSERT_TRUE(transport_result.has_value);

  _Alignas(max_align_t) unsigned char storage[4096];
  pcp_allocator_t const allocator =
      pcp_allocator_arena(storage, sizeof(storage));
  pcp_client_result_t const result = pcp_client_create(
      test_transport_interface(&transport_result.value), allocator);
  TEST_ASSERT_TRUE(result.has_value);

  pcp_client_t client = result.value;
  socket_address_t const external = {.ip = ip_address_any_v6, .port = 12345};
  CALL(&client, map, UDP, 12345, external, 60, 0, nullptr);

  pcp_client_destroy(&client);
  TEST_ASSERT_NULL(allocator.alloc(allocator.ctx, sizeof(storage)));
}

void allocator_tests() {
  RUN_TEST(test_custom_allocator_lifecycle);
  RUN_TEST(test_arena_allocator_alignment_and_exhaustion);
  RUN_TEST(test_arena_allocator_client_lifecycle);
}
