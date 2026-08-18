#pragma once

#include <pcp/pcp.h>

#include "datagram.h"
#include "internal.h"

void pcp_client_map(const pcp_client_t *client,
                    pcp_protocol_t protocol,
                    port_t internal,
                    socket_address_t external,
                    time_t ttl,
                    size_t options_count,
                    const pcp_option_t options[options_count],
                    void (*callback)(pcp_mapping_or_error_t result));

typedef struct pcp_map_response {
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  port_t internal;
  socket_address_t external;
} pcp_map_response_t;

pcp_map_response_t pcp_datagram_body_read_map_response(const_view_t datagram);
