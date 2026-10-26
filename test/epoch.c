#include <unity.h>

#include "epoch.h"
#include "runner.h"

static void test_epoch_first_response_valid() {
  pcp_epoch_t epoch = {0};
  TEST_ASSERT_TRUE(pcp_epoch_update(&epoch, 5000, 1234));
  TEST_ASSERT_TRUE(epoch.has_previous);
  TEST_ASSERT_EQUAL_UINT64(5000, epoch.client);
  TEST_ASSERT_EQUAL_UINT32(1234, epoch.server);
}

static void test_epoch_steady_clocks_valid() {
  pcp_epoch_t epoch = {0};
  pcp_epoch_update(&epoch, 0, 100);

  TEST_ASSERT_TRUE(pcp_epoch_update(&epoch, 10000, 110));
  TEST_ASSERT_TRUE(pcp_epoch_update(&epoch, 3610000, 3710));
}

static void test_epoch_slack_valid() {
  pcp_epoch_t epoch = {0};
  pcp_epoch_update(&epoch, 0, 100);

  // within 2s either way, e.g. due to the server's whole-second resolution
  TEST_ASSERT_TRUE(pcp_epoch_update(&epoch, 1900, 100));
  TEST_ASSERT_TRUE(pcp_epoch_update(&epoch, 1900, 102));
}

static void test_epoch_drift_valid() {
  pcp_epoch_t epoch = {0};
  pcp_epoch_update(&epoch, 0, 0);

  // 1/16 of 160s is 10s, plus 2s of slack
  TEST_ASSERT_TRUE(pcp_epoch_update(&epoch, 160000, 149));
}

static void test_epoch_server_went_backwards() {
  pcp_epoch_t epoch = {0};
  pcp_epoch_update(&epoch, 0, 1000);

  // e.g. the server restarted and its epoch started over
  TEST_ASSERT_FALSE(pcp_epoch_update(&epoch, 10000, 5));
}

static void test_epoch_server_too_slow() {
  pcp_epoch_t epoch = {0};
  pcp_epoch_update(&epoch, 0, 0);

  // 160s passed for us but only 140s for the server, beyond 1/16 + 2s
  TEST_ASSERT_FALSE(pcp_epoch_update(&epoch, 160000, 140));
}

static void test_epoch_server_too_fast() {
  pcp_epoch_t epoch = {0};
  pcp_epoch_update(&epoch, 0, 0);

  TEST_ASSERT_FALSE(pcp_epoch_update(&epoch, 10000, 100));
}

static void test_epoch_stored_even_if_invalid() {
  pcp_epoch_t epoch = {0};
  pcp_epoch_update(&epoch, 0, 1000);
  TEST_ASSERT_FALSE(pcp_epoch_update(&epoch, 10000, 5));

  // compared against the restarted server from now on
  TEST_ASSERT_EQUAL_UINT32(5, epoch.server);
  TEST_ASSERT_TRUE(pcp_epoch_update(&epoch, 20000, 15));
}

void epoch_tests() {
  RUN_TEST(test_epoch_first_response_valid);
  RUN_TEST(test_epoch_steady_clocks_valid);
  RUN_TEST(test_epoch_slack_valid);
  RUN_TEST(test_epoch_drift_valid);
  RUN_TEST(test_epoch_server_went_backwards);
  RUN_TEST(test_epoch_server_too_slow);
  RUN_TEST(test_epoch_server_too_fast);
  RUN_TEST(test_epoch_stored_even_if_invalid);
}
