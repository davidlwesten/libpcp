#pragma once

typedef enum pcp_protocol {
  ALL = 0,
  // https://www.iana.org/assignments/protocol-numbers
  TCP = 6,
  UDP = 17,
} pcp_protocol_t;
