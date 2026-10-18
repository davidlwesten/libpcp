#pragma once

#include <stdint.h>

typedef enum pcp_protocol : uint8_t {
  ALL = 0,
  TCP = 6,
  UDP = 17,
} pcp_protocol_t;
