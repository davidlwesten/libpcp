#pragma once

#include <pcp/ip.h>
#include <pcp/pcp.h>

socket_address_t transport_init(socket_address_t server);

// Runs until SIGINT/SIGTERM, or until the session is idle if `until_idle`
void transport_loop(pcp_session_t *session, bool until_idle);
void transport_close();
