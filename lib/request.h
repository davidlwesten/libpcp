#pragma once

#include <pcp/pcp.h>

typedef struct pcp_request {
  pcp_time_s_t lifetime;
  pcp_protocol_t protocol;
  port_t port;
  pcp_opcode_t type;
  union {
    struct {
      socket_address_t external;
    } map;
    struct {
      socket_address_t external;
      socket_address_t peer;
    } peer;
  };
} pcp_request_t;
