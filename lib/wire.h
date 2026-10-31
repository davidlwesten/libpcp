#pragma once

#include <pcp/ip.h>
#include <pcp/pcp.h>

#include "pcp_gen.h"

typedef struct const_view {
  size_t size;
  uint8_t const *data;
} const_view_t;

typedef struct view {
  size_t size;
  uint8_t *data;
} view_t;

/**
 * @brief Size of the request pcp_packet_write_request() would write
 */
size_t pcp_packet_request_size(struct pcp_request const *request,
                               size_t count,
                               pcp_option_t const options[count]);

/**
 * @brief Write a MAP or PEER request and its options
 *
 * @returns The size of the request
 */
size_t pcp_packet_write_request(view_t dst,
                                struct pcp_request const *request,
                                size_t count,
                                pcp_option_t const options[count]);

typedef struct pcp_optional_response {
  bool has_value;
  struct pcp_response value;
} pcp_optional_response_t;

/**
 * @brief Parse a response received from the server
 *
 * Only checks the format of ANNOUNCE, MAP and PEER responses, not the result
 * code. Options are skipped.
 *
 * @param[in] src The received datagram
 *
 * @returns The parsed response, its views point into `src`. Nothing if `src`
 *          isn't a valid response and should be ignored.
 */
pcp_optional_response_t pcp_packet_read_response(const_view_t src);
