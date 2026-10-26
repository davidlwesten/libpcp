#pragma once

#include <pcp/pcp.h>

typedef struct pcp_optional_count {
  bool has_value;
  uint32_t value;
} pcp_optional_count_t;

static constexpr pcp_optional_count_t PCP_BACKOFF_UNBOUNDED = {
    .has_value = false,
    .value = 0,
};

static constexpr pcp_optional_count_t PCP_BACKOFF_RELEASE = {
    .has_value = true,
    .value = 3,
};

typedef struct pcp_schedule {
  enum {
    PCP_SCHEDULE_BACKOFF,
    PCP_SCHEDULE_RENEWAL,
  } type;
  pcp_time_ms_t next;
  uint32_t count;
  union {
    struct {
      pcp_optional_count_t limit;
      pcp_time_ms_t rt;
    } backoff;
    struct {
      pcp_time_ms_t granted;
      pcp_time_ms_t lifetime;
    } renewal;
  };
} pcp_schedule_t;

typedef enum pcp_schedule_status {
  PCP_SCHEDULE_WAIT,
  PCP_SCHEDULE_DUE,
  PCP_SCHEDULE_EXHAUSTED,
} pcp_schedule_status_t;

/**
 * @brief Start a backoff with the first transmission due at `not_before`
 *
 * Pass 0 to expire right away. Exhausted once the last of `limit` transmissions
 * timed out.
 */
void pcp_schedule_backoff(pcp_schedule_t *schedule,
                          pcp_optional_count_t limit,
                          pcp_time_ms_t not_before);

/**
 * @brief Start renewing a lease granted at `granted` for `lifetime`
 *
 * Renewals happen at a random time within 1/2..5/8 of the lifetime, then
 * 3/4..13/16, 7/8..29/32 and so on, at least 4s apart. Exhausted once the lease
 * expires.
 *
 * @param[in] rng The source of randomness for the first renewal
 */
void pcp_schedule_renewal(pcp_schedule_t *schedule,
                          pcp_time_ms_t granted,
                          pcp_time_ms_t lifetime,
                          pcp_rng_t rng);

pcp_schedule_status_t pcp_schedule_poll(pcp_schedule_t const *schedule,
                                        pcp_time_ms_t now);

/**
 * @brief When pcp_schedule_poll() will next return something different
 */
pcp_time_ms_t pcp_schedule_deadline(pcp_schedule_t const *schedule);

/**
 * @brief Record a transmission at `now`
 *
 * @param[in] rng The source of randomness to jitter the next deadline
 */
void pcp_schedule_advance(pcp_schedule_t *schedule,
                          pcp_time_ms_t now,
                          pcp_rng_t rng);

/**
 * @brief Push the next transmission back to `not_before` at the earliest
 *
 * For when the server asks us to back off. Doesn't change when a renewal
 * schedule runs out.
 */
void pcp_schedule_defer(pcp_schedule_t *schedule, pcp_time_ms_t not_before);
