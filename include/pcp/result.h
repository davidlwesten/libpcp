#pragma once

#include <stddef.h>

typedef enum pcp_result {
  SUCCESS = 0,
  UNSUPP_VERSION,
  NOT_AUTHORIZED,
  MALFORMED_REQUEST,
  UNSUPP_OPCODE,
  UNSUPP_OPTION,
  MALFORMED_OPTION,
  NETWORK_FAILURE,
  NO_RESOURCES,
  UNSUPP_PROTOCOL,
  USER_EX_QUOTA,
  CANNOT_PROVIDE_EXTERNAL,
  ADDRESS_MISMATCH,
  EXCESSIVE_REMOTE_PEERS,
  SYSTEM
} pcp_result_t;

const char *pcp_strerror(pcp_result_t code);
void pcp_strerror_r(pcp_result_t code, size_t size, char buffer[size]);
