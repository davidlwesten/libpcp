#pragma once

#include <pcp/ip.h>

#include <stdint.h>

typedef struct pcp_option_third_party {
  ip_address_t internal;
} pcp_option_third_party_t;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
typedef struct pcp_option_prefer_failure {
} pcp_option_prefer_failure_t;
#pragma GCC diagnostic pop

typedef struct pcp_option_filter {
  socket_address_t peer;
  uint8_t prefix;
} pcp_option_filter_t;

typedef enum pcp_option_code {
  THIRD_PARTY = 1,
  PREFER_FAILURE = 2,
  FILTER = 3
} pcp_option_code_t;

typedef struct pcp_option {
  pcp_option_code_t code;
  union {
    pcp_option_third_party_t third_party;
    pcp_option_prefer_failure_t prefer_failure;
    pcp_option_filter_t filter;
  };
} pcp_option_t;
