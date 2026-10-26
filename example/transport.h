#pragma once

#include <pcp/ip.h>
#include <pcp/pcp.h>

socket_address_t transport_init(socket_address_t server);

// Runs the session until SIGINT or SIGTERM arrives or, with `until_idle`, until
// it has nothing left to do
void transport_loop(pcp_session_t *session, bool until_idle);
void transport_close();
