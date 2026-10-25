#include <unity.h>

#include "rng.h"
#include "runner.h"
#include "schedule.h"

// random values and the RAND they make the schedule draw
static uint16_t min_value = 0;           // -10%
static uint16_t none_value = 100;        // 0%
static uint16_t max_value = 200;         // +10%
static uint16_t wrap_value = UINT16_MAX; // 65535 % 201 = 9, i.e. -9.1%

static pcp_rng_t const JITTER_MIN = {.random = test_rng_fixed,
                                     .ctx = &min_value};
static pcp_rng_t const JITTER_NONE = {.random = test_rng_fixed,
                                      .ctx = &none_value};
static pcp_rng_t const JITTER_MAX = {.random = test_rng_fixed,
                                     .ctx = &max_value};
static pcp_rng_t const JITTER_WRAP = {.random = test_rng_fixed,
                                      .ctx = &wrap_value};

static void test_schedule_due_at_start() {
  pcp_schedule_t schedule;
  pcp_schedule_start(&schedule, PCP_BACKOFF_DEFAULT, 1'000);

  TEST_ASSERT_EQUAL(PCP_SCHEDULE_WAIT, pcp_schedule_poll(&schedule, 999));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_DUE, pcp_schedule_poll(&schedule, 1'000));
  TEST_ASSERT_EQUAL_UINT64(1'000, pcp_schedule_deadline(&schedule));
}

static void test_schedule_backoff_without_jitter() {
  pcp_schedule_t schedule;
  pcp_schedule_start(&schedule, PCP_BACKOFF_DEFAULT, 0);

  // RT doubles from IRT: 3s, 6s, 12s, 24s, ...
  pcp_time_ms_t const expected[] = {3'000,  9'000,   21'000,  45'000,
                                    93'000, 189'000, 381'000, 765'000};

  pcp_time_ms_t now = 0;
  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
    TEST_ASSERT_EQUAL(PCP_SCHEDULE_DUE, pcp_schedule_poll(&schedule, now));
    pcp_schedule_advance(&schedule, now, JITTER_NONE);
    TEST_ASSERT_EQUAL_UINT64(expected[i], pcp_schedule_deadline(&schedule));
    TEST_ASSERT_EQUAL(PCP_SCHEDULE_WAIT,
                      pcp_schedule_poll(&schedule, expected[i] - 1));
    now = expected[i];
  }
}

static void test_schedule_backoff_capped_at_mrt() {
  pcp_schedule_t schedule;
  pcp_schedule_start(&schedule, PCP_BACKOFF_DEFAULT, 0);

  for (int i = 0; i < 20; ++i) {
    pcp_schedule_advance(&schedule, 0, JITTER_NONE);
  }
  TEST_ASSERT_EQUAL_UINT64(PCP_BACKOFF_DEFAULT.mrt, schedule.rt);

  // the cap applies jitter to MRT, not to the doubled RT
  pcp_schedule_advance(&schedule, 0, JITTER_MAX);
  TEST_ASSERT_EQUAL_UINT64(1'126'400, schedule.rt);
  pcp_schedule_advance(&schedule, 0, JITTER_MIN);
  TEST_ASSERT_EQUAL_UINT64(921'600, schedule.rt);
}

static void test_schedule_jitter_bounds() {
  pcp_schedule_t schedule;

  pcp_schedule_start(&schedule, PCP_BACKOFF_DEFAULT, 0);
  pcp_schedule_advance(&schedule, 0, JITTER_MAX);
  TEST_ASSERT_EQUAL_UINT64(3'300, schedule.rt);
  pcp_schedule_advance(&schedule, 0, JITTER_MAX);
  TEST_ASSERT_EQUAL_UINT64(6'930, schedule.rt); // (2 + 0.1) * 3.3s

  pcp_schedule_start(&schedule, PCP_BACKOFF_DEFAULT, 0);
  pcp_schedule_advance(&schedule, 0, JITTER_MIN);
  TEST_ASSERT_EQUAL_UINT64(2'700, schedule.rt);
  pcp_schedule_advance(&schedule, 0, JITTER_MIN);
  TEST_ASSERT_EQUAL_UINT64(5'130, schedule.rt); // (2 - 0.1) * 2.7s
}

static void test_schedule_jitter_wraps_random() {
  pcp_schedule_t schedule;
  pcp_schedule_start(&schedule, PCP_BACKOFF_DEFAULT, 0);

  pcp_schedule_advance(&schedule, 0, JITTER_WRAP);
  TEST_ASSERT_EQUAL_UINT64(2'727, schedule.rt);
}

static void test_schedule_no_jitter_when_rng_fails() {
  pcp_schedule_t schedule;
  pcp_schedule_start(&schedule, PCP_BACKOFF_DEFAULT, 0);

  pcp_schedule_advance(&schedule, 0,
                       (pcp_rng_t){.random = test_rng_failing, .ctx = nullptr});
  TEST_ASSERT_EQUAL_UINT64(3'000, schedule.rt);
}

static void test_schedule_next_relative_to_transmission() {
  pcp_schedule_t schedule;
  pcp_schedule_start(&schedule, PCP_BACKOFF_DEFAULT, 0);

  // a late transmission pushes the retransmission back instead of bursting
  pcp_schedule_advance(&schedule, 10'000, JITTER_NONE);
  TEST_ASSERT_EQUAL_UINT64(13'000, pcp_schedule_deadline(&schedule));
}

static void test_schedule_exhausted_by_mrc() {
  pcp_backoff_t params = PCP_BACKOFF_DEFAULT;
  params.mrc = 2;

  pcp_schedule_t schedule;
  pcp_schedule_start(&schedule, params, 0);

  pcp_schedule_advance(&schedule, 0, JITTER_NONE);
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_DUE, pcp_schedule_poll(&schedule, 3'000));
  pcp_schedule_advance(&schedule, 3'000, JITTER_NONE);

  // the last transmission still gets its RT to be answered
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_WAIT, pcp_schedule_poll(&schedule, 8'999));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_EXHAUSTED,
                    pcp_schedule_poll(&schedule, 9'000));
}

static void test_schedule_exhausted_by_mrd() {
  pcp_backoff_t params = PCP_BACKOFF_DEFAULT;
  params.mrd = 5'000;

  pcp_schedule_t schedule;
  pcp_schedule_start(&schedule, params, 1'000);

  pcp_schedule_advance(&schedule, 1'000, JITTER_NONE);
  pcp_schedule_advance(&schedule, 4'000, JITTER_NONE);

  // the next transmission (10s) lies beyond MRD, so the deadline is MRD
  TEST_ASSERT_EQUAL_UINT64(6'000, pcp_schedule_deadline(&schedule));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_WAIT, pcp_schedule_poll(&schedule, 5'999));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_EXHAUSTED,
                    pcp_schedule_poll(&schedule, 6'000));
}

static void test_schedule_mrd_from_first_transmission() {
  pcp_backoff_t params = PCP_BACKOFF_DEFAULT;
  params.mrd = 5'000;

  pcp_schedule_t schedule;
  pcp_schedule_start(&schedule, params, 0);

  // not yet sent: due whatever the time, MRD hasn't started counting
  TEST_ASSERT_EQUAL_UINT64(0, pcp_schedule_deadline(&schedule));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_DUE, pcp_schedule_poll(&schedule, 100'000));

  pcp_schedule_advance(&schedule, 100'000, JITTER_NONE);
  TEST_ASSERT_EQUAL_UINT64(103'000, pcp_schedule_deadline(&schedule));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_DUE, pcp_schedule_poll(&schedule, 103'000));

  pcp_schedule_advance(&schedule, 103'000, JITTER_NONE);
  TEST_ASSERT_EQUAL_UINT64(105'000, pcp_schedule_deadline(&schedule));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_EXHAUSTED,
                    pcp_schedule_poll(&schedule, 105'000));
}

static void test_schedule_restart() {
  pcp_schedule_t schedule;
  pcp_schedule_start(&schedule, PCP_BACKOFF_DEFAULT, 0);
  for (int i = 0; i < 5; ++i) {
    pcp_schedule_advance(&schedule, 0, JITTER_NONE);
  }

  // e.g. a retryable error asking us to wait before trying again
  pcp_schedule_start(&schedule, PCP_BACKOFF_DEFAULT, 50'000);
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_WAIT, pcp_schedule_poll(&schedule, 49'999));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_DUE, pcp_schedule_poll(&schedule, 50'000));
  pcp_schedule_advance(&schedule, 50'000, JITTER_NONE);
  TEST_ASSERT_EQUAL_UINT64(53'000, pcp_schedule_deadline(&schedule));
}

void schedule_tests() {
  RUN_TEST(test_schedule_due_at_start);
  RUN_TEST(test_schedule_backoff_without_jitter);
  RUN_TEST(test_schedule_backoff_capped_at_mrt);
  RUN_TEST(test_schedule_jitter_bounds);
  RUN_TEST(test_schedule_jitter_wraps_random);
  RUN_TEST(test_schedule_no_jitter_when_rng_fails);
  RUN_TEST(test_schedule_next_relative_to_transmission);
  RUN_TEST(test_schedule_exhausted_by_mrc);
  RUN_TEST(test_schedule_exhausted_by_mrd);
  RUN_TEST(test_schedule_mrd_from_first_transmission);
  RUN_TEST(test_schedule_restart);
}
