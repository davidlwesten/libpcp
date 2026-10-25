#pragma once

#include <pcp/ip.h>
#include <pcp/pcp.h>

socket_address_t transport_init(socket_address_t server);
void transport_loop(pcp_session_t *session);
void transport_close();
