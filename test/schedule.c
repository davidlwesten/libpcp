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
  pcp_schedule_backoff(&schedule, PCP_BACKOFF_DEFAULT, 1'000);

  TEST_ASSERT_EQUAL(PCP_SCHEDULE_WAIT, pcp_schedule_poll(&schedule, 999));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_DUE, pcp_schedule_poll(&schedule, 1'000));
  TEST_ASSERT_EQUAL_UINT64(1'000, pcp_schedule_deadline(&schedule));
}

static void test_schedule_backoff_without_jitter() {
  pcp_schedule_t schedule;
  pcp_schedule_backoff(&schedule, PCP_BACKOFF_DEFAULT, 0);

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
  pcp_schedule_backoff(&schedule, PCP_BACKOFF_DEFAULT, 0);

  for (int i = 0; i < 20; ++i) {
    pcp_schedule_advance(&schedule, 0, JITTER_NONE);
  }
  TEST_ASSERT_EQUAL_UINT64(PCP_BACKOFF_DEFAULT.mrt, schedule.backoff.rt);

  // the cap applies jitter to MRT, not to the doubled RT
  pcp_schedule_advance(&schedule, 0, JITTER_MAX);
  TEST_ASSERT_EQUAL_UINT64(1'126'400, schedule.backoff.rt);
  pcp_schedule_advance(&schedule, 0, JITTER_MIN);
  TEST_ASSERT_EQUAL_UINT64(921'600, schedule.backoff.rt);
}

static void test_schedule_jitter_bounds() {
  pcp_schedule_t schedule;

  pcp_schedule_backoff(&schedule, PCP_BACKOFF_DEFAULT, 0);
  pcp_schedule_advance(&schedule, 0, JITTER_MAX);
  TEST_ASSERT_EQUAL_UINT64(3'300, schedule.backoff.rt);
  pcp_schedule_advance(&schedule, 0, JITTER_MAX);
  TEST_ASSERT_EQUAL_UINT64(6'930, schedule.backoff.rt); // (2 + 0.1) * 3.3s

  pcp_schedule_backoff(&schedule, PCP_BACKOFF_DEFAULT, 0);
  pcp_schedule_advance(&schedule, 0, JITTER_MIN);
  TEST_ASSERT_EQUAL_UINT64(2'700, schedule.backoff.rt);
  pcp_schedule_advance(&schedule, 0, JITTER_MIN);
  TEST_ASSERT_EQUAL_UINT64(5'130, schedule.backoff.rt); // (2 - 0.1) * 2.7s
}

static void test_schedule_jitter_wraps_random() {
  pcp_schedule_t schedule;
  pcp_schedule_backoff(&schedule, PCP_BACKOFF_DEFAULT, 0);

  pcp_schedule_advance(&schedule, 0, JITTER_WRAP);
  TEST_ASSERT_EQUAL_UINT64(2'727, schedule.backoff.rt);
}

static void test_schedule_no_jitter_when_rng_fails() {
  pcp_schedule_t schedule;
  pcp_schedule_backoff(&schedule, PCP_BACKOFF_DEFAULT, 0);

  pcp_schedule_advance(&schedule, 0,
                       (pcp_rng_t){.random = test_rng_failing, .ctx = nullptr});
  TEST_ASSERT_EQUAL_UINT64(3'000, schedule.backoff.rt);
}

static void test_schedule_next_relative_to_transmission() {
  pcp_schedule_t schedule;
  pcp_schedule_backoff(&schedule, PCP_BACKOFF_DEFAULT, 0);

  // a late transmission pushes the retransmission back instead of bursting
  pcp_schedule_advance(&schedule, 10'000, JITTER_NONE);
  TEST_ASSERT_EQUAL_UINT64(13'000, pcp_schedule_deadline(&schedule));
}

static void test_schedule_exhausted_by_mrc() {
  pcp_backoff_t params = PCP_BACKOFF_DEFAULT;
  params.mrc = 2;

  pcp_schedule_t schedule;
  pcp_schedule_backoff(&schedule, params, 0);

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
  pcp_schedule_backoff(&schedule, params, 1'000);

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
  pcp_schedule_backoff(&schedule, params, 0);

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
  pcp_schedule_backoff(&schedule, PCP_BACKOFF_DEFAULT, 0);
  for (int i = 0; i < 5; ++i) {
    pcp_schedule_advance(&schedule, 0, JITTER_NONE);
  }

  // e.g. a retryable error asking us to wait before trying again
  pcp_schedule_backoff(&schedule, PCP_BACKOFF_DEFAULT, 50'000);
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_WAIT, pcp_schedule_poll(&schedule, 49'999));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_DUE, pcp_schedule_poll(&schedule, 50'000));
  pcp_schedule_advance(&schedule, 50'000, JITTER_NONE);
  TEST_ASSERT_EQUAL_UINT64(53'000, pcp_schedule_deadline(&schedule));
}

// for renewals, where the random value picks a point within each window
static pcp_rng_t const WINDOW_START = JITTER_MIN;
static pcp_rng_t const WINDOW_END = JITTER_WRAP;

static constexpr pcp_time_ms_t hour = 3'600'000;

static void test_schedule_renewal_first_window() {
  pcp_schedule_t schedule;

  // 1/2 to 5/8 of the lifetime, counted from when the lease was granted
  pcp_schedule_renewal(&schedule, 1'000, hour, WINDOW_START);
  TEST_ASSERT_EQUAL_UINT64(1'000 + hour / 2, pcp_schedule_deadline(&schedule));
  pcp_schedule_renewal(&schedule, 1'000, hour, WINDOW_END);
  TEST_ASSERT_EQUAL_UINT64(1'000 + hour * 5 / 8,
                           pcp_schedule_deadline(&schedule));

  TEST_ASSERT_EQUAL(PCP_SCHEDULE_WAIT,
                    pcp_schedule_poll(&schedule, 1'000 + hour * 5 / 8 - 1));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_DUE,
                    pcp_schedule_poll(&schedule, 1'000 + hour * 5 / 8));
}

static void test_schedule_renewal_later_windows() {
  pcp_schedule_t schedule;
  pcp_schedule_renewal(&schedule, 0, hour, WINDOW_START);

  // 3/4 to 3/4 + 1/16
  pcp_schedule_advance(&schedule, hour / 2, WINDOW_START);
  TEST_ASSERT_EQUAL_UINT64(hour * 3 / 4, pcp_schedule_deadline(&schedule));

  // 7/8 to 7/8 + 1/32
  pcp_schedule_advance(&schedule, hour * 3 / 4, WINDOW_END);
  TEST_ASSERT_EQUAL_UINT64(hour * 7 / 8 + hour / 32,
                           pcp_schedule_deadline(&schedule));

  pcp_schedule_advance(&schedule, hour * 7 / 8, WINDOW_START);
  TEST_ASSERT_EQUAL_UINT64(hour * 15 / 16, pcp_schedule_deadline(&schedule));
  TEST_ASSERT_EQUAL_UINT32(3, schedule.count);
}

static void test_schedule_renewal_at_least_4s_apart() {
  pcp_schedule_t schedule;
  pcp_schedule_renewal(&schedule, 0, 10'000, WINDOW_START);
  TEST_ASSERT_EQUAL_UINT64(5'000, pcp_schedule_deadline(&schedule));

  // 3/4 would be 7.5s, but that's only 2.5s after the previous renewal
  pcp_schedule_advance(&schedule, 5'000, WINDOW_START);
  TEST_ASSERT_EQUAL_UINT64(9'000, pcp_schedule_deadline(&schedule));

  // no room for another one before the lease expires
  pcp_schedule_advance(&schedule, 9'000, WINDOW_START);
  TEST_ASSERT_EQUAL_UINT64(10'000, pcp_schedule_deadline(&schedule));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_WAIT, pcp_schedule_poll(&schedule, 9'999));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_EXHAUSTED,
                    pcp_schedule_poll(&schedule, 10'000));
}

static void test_schedule_renewal_exhausted_at_expiry() {
  pcp_schedule_t schedule;
  pcp_schedule_renewal(&schedule, 1'000, hour, WINDOW_START);

  // even if a renewal would be due, the lease is gone
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_DUE,
                    pcp_schedule_poll(&schedule, 1'000 + hour - 1));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_EXHAUSTED,
                    pcp_schedule_poll(&schedule, 1'000 + hour));
}

static void test_schedule_defer() {
  pcp_schedule_t schedule;
  pcp_schedule_renewal(&schedule, 0, hour, WINDOW_START);

  pcp_schedule_defer(&schedule, hour / 2 + 30'000);
  TEST_ASSERT_EQUAL_UINT64(hour / 2 + 30'000, pcp_schedule_deadline(&schedule));

  // never brings the next transmission forward
  pcp_schedule_defer(&schedule, 1'000);
  TEST_ASSERT_EQUAL_UINT64(hour / 2 + 30'000, pcp_schedule_deadline(&schedule));

  // and the lease still expires
  pcp_schedule_defer(&schedule, hour + 30'000);
  TEST_ASSERT_EQUAL_UINT64(hour, pcp_schedule_deadline(&schedule));
  TEST_ASSERT_EQUAL(PCP_SCHEDULE_EXHAUSTED, pcp_schedule_poll(&schedule, hour));
}

void schedule_tests() {
  RUN_TEST(test_schedule_due_at_start);
  RUN_TEST(test_schedule_renewal_first_window);
  RUN_TEST(test_schedule_renewal_later_windows);
  RUN_TEST(test_schedule_renewal_at_least_4s_apart);
  RUN_TEST(test_schedule_renewal_exhausted_at_expiry);
  RUN_TEST(test_schedule_defer);
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
