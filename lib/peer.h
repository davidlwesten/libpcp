#pragma once

#include <pcp/pcp.h>

#include "request.h"
#include "response.h"
#include "types.h"

void pcp_client_peer_request(pcp_client_t *client,
                             pcp_protocol_t protocol,
                             port_t internal,
                             socket_address_t external,
                             socket_address_t peer,
                             time_t ttl,
                             size_t options_count,
                             const pcp_option_t options[options_count]);

const_view_t pcp_packet_write_peer_request(view_t dst,
                                           pcp_peer_request_t const *src);

pcp_peer_request_t
pcp_packet_body_read_peer_request(pcp_request_header_t header,
                                  const_view_t packet);

pcp_peer_response_t
pcp_packet_body_read_peer_response(pcp_response_header_t header,
                                   const_view_t packet);
