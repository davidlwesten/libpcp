#include "schedule.h"

#include <assert.h>
#include <stdint.h>

// IRT and MRT from RFC 6887 section 8.1.1
static constexpr pcp_time_ms_t PCP_BACKOFF_IRT = 3000;
static constexpr pcp_time_ms_t PCP_BACKOFF_MRT = 1024000;

// minimum gap between renewals (RFC 6887 section 11.2.1)
static constexpr pcp_time_ms_t PCP_RENEWAL_MIN_INTERVAL = 4000;

// returns false if the RNG failed
static bool random16(pcp_rng_t rng, uint16_t *out) {
  return rng.fill(rng.ctx, sizeof(*out), (uint8_t *)out);
}

void pcp_schedule_backoff(pcp_schedule_t *schedule,
                          pcp_optional_count_t limit,
                          pcp_time_ms_t not_before) {
  assert(schedule);

  *schedule = (pcp_schedule_t){
      .type = PCP_SCHEDULE_BACKOFF,
      .next = not_before,
      .count = 0,
      .backoff = {.limit = limit, .rt = 0},
  };
}

static pcp_time_ms_t pcp_schedule_expiry(pcp_schedule_t const *schedule) {
  return schedule->renewal.granted + schedule->renewal.lifetime;
}

// Renewal n (from 0) happens at 1 - 1/2^(n+1) of the lifetime plus a random
// part of a window 1/2^(n+3) wide: 1/2..5/8, 3/4..13/16, 7/8..29/32, ...
static pcp_time_ms_t pcp_schedule_renewal_time(pcp_schedule_t const *schedule,
                                               uint32_t attempt,
                                               pcp_rng_t rng) {
  auto const lifetime = schedule->renewal.lifetime;

  // the window is long gone by now
  if (attempt + 3 >= 64) {
    return pcp_schedule_expiry(schedule);
  }

  auto const start = lifetime - (lifetime >> (attempt + 1));
  auto const width = lifetime >> (attempt + 3);
  uint16_t random = 0;
  if (!random16(rng, &random)) {
    random = 0; // start of the window
  }
  auto const offset = width * random / UINT16_MAX;
  return schedule->renewal.granted + start + offset;
}

void pcp_schedule_renewal(pcp_schedule_t *schedule,
                          pcp_time_ms_t granted,
                          pcp_time_ms_t lifetime,
                          pcp_rng_t rng) {
  assert(schedule);
  assert(rng.fill);

  *schedule = (pcp_schedule_t){
      .type = PCP_SCHEDULE_RENEWAL,
      .count = 0,
      .renewal = {.granted = granted, .lifetime = lifetime},
  };
  schedule->next = pcp_schedule_renewal_time(schedule, 0, rng);
}

pcp_time_ms_t pcp_schedule_deadline(pcp_schedule_t const *schedule) {
  assert(schedule);

  // a renewal schedule runs out when the lease expires, even if a renewal is
  // still due after that
  if (schedule->type == PCP_SCHEDULE_RENEWAL &&
      pcp_schedule_expiry(schedule) < schedule->next) {
    return pcp_schedule_expiry(schedule);
  }

  return schedule->next;
}

pcp_schedule_status_t pcp_schedule_poll(pcp_schedule_t const *schedule,
                                        pcp_time_ms_t now) {
  assert(schedule);

  if (now < pcp_schedule_deadline(schedule)) {
    return PCP_SCHEDULE_WAIT;
  }

  switch (schedule->type) {
  case PCP_SCHEDULE_BACKOFF: {
    auto const limit = schedule->backoff.limit;
    return limit.has_value && schedule->count >= limit.value
               ? PCP_SCHEDULE_EXHAUSTED
               : PCP_SCHEDULE_DUE;
  }
  case PCP_SCHEDULE_RENEWAL:
    return now >= pcp_schedule_expiry(schedule) ? PCP_SCHEDULE_EXHAUSTED
                                                : PCP_SCHEDULE_DUE;
  }
}

// RAND from RFC 6887 section 8.1.1 in per mille (-100 to 100), 0 if the RNG
// fails. The modulo bias doesn't matter here.
static int32_t jitter(pcp_rng_t rng) {
  constexpr int32_t max = 100;
  uint16_t random = 0;
  if (!random16(rng, &random)) {
    return 0;
  }
  return (int32_t)(random % (2 * max + 1)) - max;
}

// value * (1000 + permille) / 1000
static pcp_time_ms_t scale(pcp_time_ms_t value, int32_t permille) {
  assert(permille > -1000);
  return value * (pcp_time_ms_t)(1000 + permille) / 1000;
}

static void pcp_schedule_advance_backoff(pcp_schedule_t *schedule,
                                         pcp_time_ms_t now,
                                         pcp_rng_t rng) {
  auto const backoff = &schedule->backoff;
  auto const rand = jitter(rng);

  // RT = (1 + RAND) * IRT for the first transmission, (2 + RAND) * RTprev after
  // that, and (1 + RAND) * MRT once that would exceed MRT
  auto rt = schedule->count == 0 ? scale(PCP_BACKOFF_IRT, rand)
                                 : scale(backoff->rt, 1000 + rand);
  if (rt > PCP_BACKOFF_MRT) {
    rt = scale(PCP_BACKOFF_MRT, rand);
  }

  backoff->rt = rt;
  schedule->next = now + rt;
}

static void pcp_schedule_advance_renewal(pcp_schedule_t *schedule,
                                         pcp_time_ms_t now,
                                         pcp_rng_t rng) {
  // count doesn't include this transmission yet
  auto const next =
      pcp_schedule_renewal_time(schedule, schedule->count + 1, rng);
  auto const earliest = now + PCP_RENEWAL_MIN_INTERVAL;
  schedule->next = next > earliest ? next : earliest;
}

void pcp_schedule_advance(pcp_schedule_t *schedule,
                          pcp_time_ms_t now,
                          pcp_rng_t rng) {
  assert(schedule);
  assert(rng.fill);

  switch (schedule->type) {
  case PCP_SCHEDULE_BACKOFF:
    pcp_schedule_advance_backoff(schedule, now, rng);
    break;
  case PCP_SCHEDULE_RENEWAL:
    pcp_schedule_advance_renewal(schedule, now, rng);
    break;
  }
  schedule->count += 1;
}

void pcp_schedule_defer(pcp_schedule_t *schedule, pcp_time_ms_t not_before) {
  assert(schedule);

  if (schedule->next < not_before) {
    schedule->next = not_before;
  }
}
