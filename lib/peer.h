#pragma once

#include <pcp/pcp.h>

pcp_mapping_or_error_t
pcp_client_peer(const pcp_client_t *client,
                pcp_protocol_t protocol,
                port_t internal,
                socket_address_t external,
                socket_address_t peer,
                time_t ttl,
                size_t options_count,
                const pcp_option_t options[options_count]);
