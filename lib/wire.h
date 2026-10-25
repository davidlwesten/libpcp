#pragma once

#include <pcp/ip.h>
#include <pcp/pcp.h>

typedef struct const_view {
  size_t size;
  uint8_t const *data;
} const_view_t;

typedef struct view {
  size_t size;
  uint8_t *data;
} view_t;

typedef struct pcp_request pcp_request_t;

size_t pcp_packet_write_request(view_t dst,
                                pcp_request_t const *src,
                                ip_address_t const *internal,
                                size_t count,
                                pcp_option_t const options[count]);
