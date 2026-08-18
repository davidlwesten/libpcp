#pragma once

#include <pcp/option.h>

#include "types.h"

#include <stddef.h>

constexpr size_t PCP_THIRD_PARTY_OPTION_SIZE = 20;
constexpr size_t PCP_PREFER_FAILURE_OPTION_SIZE = 4;
constexpr size_t PCP_FILTER_OPTION_SIZE = 24;

void pcp_datagram_body_write_third_party(view_t dst,
                                         size_t offset,
                                         const pcp_option_third_party_t *src);

void pcp_datagram_body_write_prefer_failure(
    view_t dst,
    size_t offset,
    const pcp_option_prefer_failure_t *src);

void pcp_datagram_body_write_filter(view_t dst,
                                    size_t offset,
                                    const pcp_option_filter_t *src);
