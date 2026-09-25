#pragma once

#include "packet.h"
#include "response.h"

#include <inttypes.h>
#include <time.h>

typedef enum pcp_lease_state {
  PENDING,
  ACTIVE,
  RENEWING,
  FAILED
} pcp_lease_state_t;

typedef struct pcp_response_header_or_error {
  bool has_value;
  union {
    pcp_response_header_t value;
    errno_t error;
  };
} pcp_response_header_or_error_t;

typedef struct pcp_rtx_timer pcp_rtx_timer_t;

typedef struct pcp_lease_retry_result {
  bool should_retry;
  pcp_transport_timeout_t timeout;
} pcp_lease_retry_result_t;

typedef pcp_lease_retry_result_t (*pcp_lease_retry_advance_t)(
    pcp_rtx_timer_t *retry,
    time_t now);

struct pcp_rtx_timer {
  pcp_transport_timer_t timer;
  pcp_transport_timeout_t timeout;
  time_t max_timeout;
  time_t max_duration;
  uint8_t count;
  uint8_t max_count;
  pcp_lease_retry_advance_t advance;
};

typedef struct pcp_lease_common {
  pcp_transport_t transport;
  time_t (*time)();
  packet_t *request;
} pcp_lease_common_t;

typedef struct pcp_lease_pending {
  pcp_rtx_timer_t rtx;
} pcp_lease_pending_t;

typedef struct pcp_lease_active {
  pcp_response_header_t header;
  pcp_transport_timer_t timer;
  time_t expiration;
} pcp_lease_active_t;

typedef struct pcp_lease_renewing {
  pcp_rtx_timer_t rtx;
  time_t expiration;
} pcp_lease_renewing_t;

typedef struct pcp_lease_failed {
  pcp_response_header_or_error_t header;
} pcp_lease_failed_t;

typedef struct pcp_lease {
  pcp_lease_state_t state;
  pcp_lease_common_t common;
  union {
    pcp_lease_pending_t pending;
    pcp_lease_active_t active;
    pcp_lease_renewing_t renewing;
    pcp_lease_failed_t failed;
  };
} pcp_lease_t;

void pcp_lease_on_recv(pcp_lease_t *lease, pcp_response_t const *response);
void pcp_lease_destroy(pcp_lease_t *lease);

void pcp_pending_state_create(pcp_lease_t *lease,
                              pcp_transport_t transport,
                              time_t (*time)(),
                              packet_t *packet);
void pcp_lease_start(pcp_lease_t *lease,
                     pcp_transport_t transport,
                     time_t (*time)(),
                     packet_t *packet);

void pcp_failed_state_create(pcp_lease_t *lease,
                             pcp_response_header_or_error_t header);
