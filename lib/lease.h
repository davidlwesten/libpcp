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
} pcp_lease_state_t;

typedef union pcp_lease pcp_lease_t;

typedef void (*pcp_lease_state_update_t)(pcp_lease_t *mapping,
                                         pcp_response_t const *response);

typedef struct pcp_lease_state_base {
  pcp_lease_state_t state;
  packet_t *request;
} pcp_lease_state_base_t;

constexpr time_t PCP_MRT = 1024;
constexpr time_t PCP_MRD = 0;
constexpr uint8_t PCP_MRC = 0;

typedef struct pcp_rtx_timer {
  transport_timer_t timer;
  time_t timeout;
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
} pcp_mapping_state_active_t;

typedef struct pcp_lease_state_error {
  pcp_lease_state_base_t base;
} pcp_mapping_state_error_t;

typedef union pcp_lease {
  pcp_lease_state_base_t base;
  pcp_lease_state_pending_t pending;
  pcp_mapping_state_active_t active;
  pcp_mapping_state_error_t error;
} pcp_lease_t;

void pcp_lease_update(pcp_lease_t *mapping, pcp_response_t const *response);

pcp_lease_t pcp_lease_create_pending(transport_t *transport,
                                     packet_t *packet,
                                     size_t index);
