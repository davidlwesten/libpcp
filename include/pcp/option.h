#pragma once

#include <pcp/ip.h>

#include <stdint.h>

typedef enum pcp_option_code : uint8_t {
  THIRD_PARTY = 0x01,
  PREFER_FAILURE = 0x02,
  FILTER = 0x03,
  REQUIRED = 0x80,
} pcp_option_code_t;

typedef struct pcp_option_third_party {
  pcp_option_code_t code;
  ip_address_t internal;
} pcp_option_third_party_t;

typedef struct pcp_option_prefer_failure {
  pcp_option_code_t code;
} pcp_option_prefer_failure_t;

typedef struct pcp_option_filter {
  pcp_option_code_t code;
  socket_address_t peer;
  uint8_t prefix;
} pcp_option_filter_t;

typedef union pcp_option {
  pcp_option_code_t code;
  pcp_option_third_party_t third_party;
  pcp_option_prefer_failure_t prefer_failure;
  pcp_option_filter_t filter;
} pcp_option_t;

pcp_option_t pcp_option_create_third_party(bool required,
                                           ip_address_t internal);
pcp_option_t pcp_option_create_prefer_failure(bool required);
pcp_option_t
pcp_option_create_filter(bool required, socket_address_t peer, uint8_t prefix);
