#pragma once

#include <pcp/pcp.h>

typedef struct pcp_backoff {
  pcp_time_ms_t irt; // initial retransmission time, must be non-zero
  pcp_time_ms_t mrt; // maximum retransmission time, must be non-zero
  pcp_time_ms_t mrd; // maximum retransmission duration, 0 = unbounded
  uint32_t mrc;      // maximum retransmission count, 0 = unbounded
} pcp_backoff_t;

static constexpr pcp_backoff_t PCP_BACKOFF_DEFAULT = {
    .irt = 3'000,
    .mrt = 1'024'000,
    .mrd = 0,
    .mrc = 0,
};

typedef struct pcp_schedule {
  pcp_backoff_t params;
  pcp_time_ms_t start; // first transmission, valid once count > 0
  pcp_time_ms_t next;  // next transmission due
  pcp_time_ms_t rt;    // current retransmission interval
  uint32_t count;      // transmissions so far
} pcp_schedule_t;

typedef enum pcp_schedule_status {
  PCP_SCHEDULE_WAIT,      // nothing to do before pcp_schedule_deadline()
  PCP_SCHEDULE_DUE,       // a transmission is due
  PCP_SCHEDULE_EXHAUSTED, // MRC/MRD reached and the last RT has elapsed
} pcp_schedule_status_t;

/**
 * @brief (Re)start a schedule whose first transmission is due at `not_before`
 *
 * Pass 0 for a transmission that is due immediately, whatever the current time.
 * MRD is measured from the first transmission, not from `not_before`.
 */
void pcp_schedule_start(pcp_schedule_t *schedule,
                        pcp_backoff_t params,
                        pcp_time_ms_t not_before);

pcp_schedule_status_t pcp_schedule_poll(pcp_schedule_t const *schedule,
                                        pcp_time_ms_t now);

/**
 * @brief The next point in time at which pcp_schedule_poll() changes its result
 */
pcp_time_ms_t pcp_schedule_deadline(pcp_schedule_t const *schedule);

/**
 * @brief Record a transmission at `now`
 *
 * @param[in] rng The source of randomness for the retransmission interval
 */
void pcp_schedule_advance(pcp_schedule_t *schedule,
                          pcp_time_ms_t now,
                          pcp_rng_t rng);
