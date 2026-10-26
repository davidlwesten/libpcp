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

// deleting a mapping is worth a few attempts, if they all go unanswered the
// mapping simply expires at the end of its lease
static constexpr pcp_backoff_t PCP_BACKOFF_RELEASE = {
    .irt = 3'000,
    .mrt = 1'024'000,
    .mrd = 0,
    .mrc = 3,
};

typedef struct pcp_schedule {
  enum {
    PCP_SCHEDULE_BACKOFF, // retransmissions of a request, RFC 6887 §8.1.1
    PCP_SCHEDULE_RENEWAL, // renewals of a lease, RFC 6887 §11.2.1
  } type;
  pcp_time_ms_t next; // next transmission due
  uint32_t count;     // transmissions so far
  union {
    struct {
      pcp_backoff_t params;
      pcp_time_ms_t start; // first transmission, valid once count > 0
      pcp_time_ms_t rt;    // current retransmission interval
    } backoff;
    struct {
      pcp_time_ms_t granted;  // when the lease was granted
      pcp_time_ms_t lifetime; // the lease's lifetime
    } renewal;
  };
} pcp_schedule_t;

typedef enum pcp_schedule_status {
  PCP_SCHEDULE_WAIT,      // nothing to do before pcp_schedule_deadline()
  PCP_SCHEDULE_DUE,       // a transmission is due
  PCP_SCHEDULE_EXHAUSTED, // MRC/MRD reached and the last RT has elapsed, or
                          // the lease expired
} pcp_schedule_status_t;

/**
 * @brief (Re)start a backoff whose first transmission is due at `not_before`
 *
 * Pass 0 for a transmission that is due immediately, whatever the current time.
 * MRD is measured from the first transmission, not from `not_before`.
 */
void pcp_schedule_backoff(pcp_schedule_t *schedule,
                          pcp_backoff_t params,
                          pcp_time_ms_t not_before);

/**
 * @brief Start renewing a lease granted at `granted` for `lifetime`
 *
 * Renewals are due at a random point within 1/2 to 5/8 of the lifetime, then
 * 3/4 to 3/4 + 1/16, 7/8 to 7/8 + 1/32 and so on, at least 4s apart. The
 * schedule is exhausted once the lease expires.
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
 * @brief The next point in time at which pcp_schedule_poll() changes its result
 */
pcp_time_ms_t pcp_schedule_deadline(pcp_schedule_t const *schedule);

/**
 * @brief Record a transmission at `now`
 *
 * @param[in] rng The source of randomness for the next transmission
 */
void pcp_schedule_advance(pcp_schedule_t *schedule,
                          pcp_time_ms_t now,
                          pcp_rng_t rng);

/**
 * @brief Make sure nothing is due before `not_before`
 *
 * E.g. because the server asked not to be sent the same request again before
 * then. A renewal schedule still runs out when the lease expires.
 */
void pcp_schedule_defer(pcp_schedule_t *schedule, pcp_time_ms_t not_before);
