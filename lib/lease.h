#pragma once

#include "packet.h"
#include "response.h"
#include "transport.h"

#include <inttypes.h>
#include <time.h>

typedef enum pcp_lease_state {
  PENDING,
  ACTIVE,
  ERROR,
  FAILED
} pcp_lease_state_t;

typedef struct pcp_lease_state_base {
  pcp_lease_state_t state;
  packet_t *request;
} pcp_lease_state_base_t;

constexpr time_t PCP_MRT = 1024;
constexpr time_t PCP_MRD = 0;
constexpr uint8_t PCP_MRC = 0;

typedef struct pcp_rtx_timer {
  transport_timer_t timer;
  transport_timeout_t timeout;
  time_t max_time;
  time_t max_duration;
  uint8_t count;
  uint8_t max_count;
} pcp_rtx_timer_t;

typedef struct pcp_lease_state_pending {
  pcp_lease_state_base_t base;
  time_t start;
  pcp_rtx_timer_t rtx;
} pcp_lease_state_pending_t;

typedef struct pcp_lease_state_active {
  pcp_lease_state_base_t base;
  pcp_response_header_t header;
  transport_timer_t timer;
} pcp_lease_state_active_t;

typedef struct pcp_lease_state_error {
  pcp_lease_state_base_t base;
  pcp_response_header_t header;
  transport_timer_t timer;
} pcp_lease_state_error_t;

typedef struct pcp_response_header_or_error {
  bool has_value;
  union {
    pcp_response_header_t value;
    errno_t error;
  };
} pcp_response_header_or_error_t;

typedef struct pcp_lease_state_failed {
  pcp_lease_state_base_t base;
  pcp_response_header_or_error_t header;
} pcp_lease_state_failed_t;

typedef union pcp_lease {
  pcp_lease_state_base_t base;
  pcp_lease_state_pending_t pending;
  pcp_lease_state_active_t active;
  pcp_lease_state_error_t error;
  pcp_lease_state_failed_t failed;
} pcp_lease_t;

void pcp_lease_on_recv(pcp_lease_t *mapping,
                       transport_t *transport,
                       pcp_response_t const *response);

void pcp_pending_state_create(pcp_lease_t *lease,
                              transport_t *transport,
                              packet_t *packet);

void pcp_failed_state_create(pcp_lease_t *lease,
                             pcp_response_header_or_error_t header,
                             packet_t *packet);
