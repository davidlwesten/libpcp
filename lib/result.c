#include <pcp/result.h>

#include <assert.h>
#include <string.h>

const char *pcp_strerror(pcp_result_t result) {
  switch (result) {
  case SUCCESS:
    return "SUCCESS";
  case UNSUPP_VERSION:
    return "UNSUPP_VERSION";
  case NOT_AUTHORIZED:
    return "NOT_AUTHORIZED";
  case MALFORMED_REQUEST:
    return "MALFORMED_REQUEST";
  case UNSUPP_OPCODE:
    return "UNSUPP_OPCODE";
  case UNSUPP_OPTION:
    return "UNSUPP_OPTION";
  case MALFORMED_OPTION:
    return "MALFORMED_OPTION";
  case NETWORK_FAILURE:
    return "NETWORK_FAILURE";
  case NO_RESOURCES:
    return "NO_RESOURCES";
  case UNSUPP_PROTOCOL:
    return "UNSUPP_PROTOCOL";
  case USER_EX_QUOTA:
    return "USER_EX_QUOTA";
  case CANNOT_PROVIDE_EXTERNAL:
    return "CANNOT_PROVIDE_EXTERNAL";
  case ADDRESS_MISMATCH:
    return "ADDRESS_MISMATCH";
  case EXCESSIVE_REMOTE_PEERS:
    return "EXCESSIVE_REMOTE_PEERS";
  }
  return "Unexpected result code";
}

void pcp_strerror_r(pcp_result_t result, size_t size, char dst[size]) {
  memset(dst, 0, size);
  strncpy(dst, pcp_strerror(result), size - 1);
}
