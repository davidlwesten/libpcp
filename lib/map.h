#pragma once

#include <pcp/pcp.h>

#include "request.h"
#include "response.h"
#include "types.h"

typedef struct pcp_client_internal pcp_client_internal_t;

void pcp_client_map_request(pcp_client_internal_t *client,
                            pcp_protocol_t protocol,
                            port_t internal,
                            socket_address_t external,
                            time_t ttl,
                            size_t options_count,
                            const pcp_option_t options[options_count]);

pcp_transport_const_view_t pcp_packet_write_map_request(pcp_transport_view_t dst,
                                          pcp_map_request_t const *src);

pcp_map_request_t pcp_packet_body_read_map_request(pcp_request_header_t header,
                                                   pcp_transport_const_view_t packet);

pcp_map_response_t
pcp_packet_body_read_map_response(pcp_response_header_t header,
                                  pcp_transport_const_view_t packet);
