#pragma once

#include <pcp/pcp.h>

typedef struct pcp_backoff {
  /// Initial retransmission time, must be non-zero
  pcp_time_ms_t irt;
  /// Maximum retransmission time, must be non-zero
  pcp_time_ms_t mrt;
  /// Maximum retransmission duration, 0 = unbounded
  pcp_time_ms_t mrd;
  /// Maximum retransmission count, 0 = unbounded
  uint32_t mrc;
} pcp_backoff_t;

static constexpr pcp_backoff_t PCP_BACKOFF_DEFAULT = {
    .irt = 3'000,
    .mrt = 1'024'000,
    .mrd = 0,
    .mrc = 0,
};

/**
 * @brief For delete requests. If none get an answer the mapping just expires.
 */
static constexpr pcp_backoff_t PCP_BACKOFF_RELEASE = {
    .irt = 3'000,
    .mrt = 1'024'000,
    .mrd = 0,
    .mrc = 3,
};

typedef struct pcp_schedule {
  enum {
    /// Request retransmissions (RFC 6887 section 8.1.1)
    PCP_SCHEDULE_BACKOFF,
    /// Lease renewals (RFC 6887 section 11.2.1)
    PCP_SCHEDULE_RENEWAL,
  } type;
  /// Next transmission due
  pcp_time_ms_t next;
  /// Transmissions so far
  uint32_t count;
  union {
    struct {
      pcp_backoff_t params;
      /// First transmission, valid once count > 0
      pcp_time_ms_t start;
      /// Current retransmission interval
      pcp_time_ms_t rt;
    } backoff;
    struct {
      /// When the lease was granted
      pcp_time_ms_t granted;
      /// The lease's lifetime
      pcp_time_ms_t lifetime;
    } renewal;
  };
} pcp_schedule_t;

typedef enum pcp_schedule_status {
  /// Nothing to do before pcp_schedule_deadline()
  PCP_SCHEDULE_WAIT,
  /// A transmission is due
  PCP_SCHEDULE_DUE,
  /// Out of retries, or the lease expired
  PCP_SCHEDULE_EXHAUSTED,
} pcp_schedule_status_t;

/**
 * @brief Start a backoff with the first transmission due at `not_before`
 *
 * Pass 0 to send right away. MRD counts from the first transmission.
 */
void pcp_schedule_backoff(pcp_schedule_t *schedule,
                          pcp_backoff_t params,
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
 * @param[in] rng The source of randomness for the next transmission
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
