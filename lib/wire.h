#pragma once

#include <pcp/ip.h>
#include <pcp/pcp.h>

#include "nonce.h"

typedef struct const_view {
  size_t size;
  uint8_t const *data;
} const_view_t;

typedef struct view {
  size_t size;
  uint8_t *data;
} view_t;

typedef struct pcp_request pcp_request_t;
typedef struct pcp_response pcp_response_t;

/**
 * @brief Size of the request pcp_packet_write_request() would write
 */
size_t pcp_packet_request_size(pcp_request_t const *request,
                               size_t count,
                               pcp_option_t const options[count]);

/**
 * @brief Write a request and its options
 *
 * @returns The size of the request
 */
size_t pcp_packet_write_request(view_t dst,
                                pcp_request_t const *src,
                                pcp_nonce_t const *nonce,
                                ip_address_t const *internal,
                                size_t count,
                                pcp_option_t const options[count]);

/**
 * @brief Parse a response received from the server
 *
 * Only checks the format of ANNOUNCE, MAP and PEER responses, not the result
 * code. Options are skipped.
 *
 * @param[in]  src The received datagram
 * @param[out] out The parsed response
 *
 * @returns `false` if `src` isn't a valid response and should be ignored
 */
bool pcp_packet_read_response(const_view_t src, pcp_response_t *out);
