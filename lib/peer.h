#pragma once

#include <pcp/pcp.h>

#include "client.h"
#include "types.h"

void pcp_client_peer_request(const pcp_client_t *client,
                             pcp_protocol_t protocol,
                             port_t internal,
                             socket_address_t external,
                             socket_address_t peer,
                             time_t ttl,
                             size_t options_count,
                             const pcp_option_t options[options_count],
                             void (*callback)(pcp_mapping_or_error_t result));

typedef struct pcp_peer_response {
  pcp_nonce_t nonce;
  pcp_protocol_t protocol;
  port_t internal;
  socket_address_t external;
  socket_address_t peer;
} pcp_peer_response_t;

pcp_peer_response_t pcp_datagram_body_read_peer_response(const_view_t datagram);
