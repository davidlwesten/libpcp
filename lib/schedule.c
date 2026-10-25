#include "schedule.h"

#include <assert.h>

void pcp_schedule_start(pcp_schedule_t *schedule,
                        pcp_backoff_t params,
                        pcp_time_ms_t not_before) {
  assert(schedule);
  assert(params.irt && params.mrt);

  *schedule = (pcp_schedule_t){
      .params = params,
      .start = 0,
      .next = not_before,
      .rt = 0,
      .count = 0,
  };
}

// MRD only applies once the first transmission happened
static bool pcp_schedule_has_mrd(pcp_schedule_t const *schedule) {
  return schedule->params.mrd && schedule->count > 0;
}

static bool pcp_schedule_mrd_reached(pcp_schedule_t const *schedule,
                                     pcp_time_ms_t now) {
  return pcp_schedule_has_mrd(schedule) &&
         now >= schedule->start + schedule->params.mrd;
}

static bool pcp_schedule_mrc_reached(pcp_schedule_t const *schedule) {
  return schedule->params.mrc && schedule->count >= schedule->params.mrc;
}

pcp_time_ms_t pcp_schedule_deadline(pcp_schedule_t const *schedule) {
  assert(schedule);

  if (pcp_schedule_has_mrd(schedule)) {
    auto const limit = schedule->start + schedule->params.mrd;
    return limit < schedule->next ? limit : schedule->next;
  }

  return schedule->next;
}

pcp_schedule_status_t pcp_schedule_poll(pcp_schedule_t const *schedule,
                                        pcp_time_ms_t now) {
  assert(schedule);

  if (now < pcp_schedule_deadline(schedule)) {
    return PCP_SCHEDULE_WAIT;
  }

  if (pcp_schedule_mrc_reached(schedule) ||
      pcp_schedule_mrd_reached(schedule, now)) {
    return PCP_SCHEDULE_EXHAUSTED;
  }

  return PCP_SCHEDULE_DUE;
}

// RAND from RFC 6887 §8.1.1 in per mille, uniformly within [-100, 100], or 0
// if the RNG fails. The modulo bias (< 0.5%) is irrelevant for jitter
static int32_t jitter(pcp_rng_t rng) {
  uint16_t random = 0;
  if (!rng.random(rng.ctx, sizeof(random), (uint8_t *)&random)) {
    return 0;
  }

  constexpr int32_t max = 100;
  return (int32_t)(random % (2 * max + 1)) - max;
}

// value * (1000 + permille) / 1000
static pcp_time_ms_t scale(pcp_time_ms_t value, int32_t permille) {
  assert(permille > -1000);
  return value * (pcp_time_ms_t)(1000 + permille) / 1000;
}

void pcp_schedule_advance(pcp_schedule_t *schedule,
                          pcp_time_ms_t now,
                          pcp_rng_t rng) {
  assert(schedule);
  assert(rng.random);

  auto const rand = jitter(rng);

  // RT = (1 + RAND) * IRT for the first transmission, (2 + RAND) * RTprev after
  // that, and (1 + RAND) * MRT once that would exceed MRT
  auto rt = schedule->count == 0 ? scale(schedule->params.irt, rand)
                                 : scale(schedule->rt, 1000 + rand);
  if (rt > schedule->params.mrt) {
    rt = scale(schedule->params.mrt, rand);
  }

  if (schedule->count == 0) {
    schedule->start = now;
  }
  schedule->rt = rt;
  schedule->next = now + rt;
  schedule->count += 1;
}
