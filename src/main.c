#include <pcp/pcp.h>

#include <stdio.h>

int main() {
  pcp_client_t *client = pcp_client_create("127.0.0.1");
  if (!client) {
    fprintf(stderr, "pcp_client_create failed!\n");
    return 1;
  }

  constexpr port_t internal = 12000;
  constexpr socket_address_t external = {.ip = ip_address_any_v6,
                                         .port = 12000};
  const socket_address_t peer = {.ip = ip_address_from_octets(192, 168, 8, 2),
                                 .port = 12000};

  CALL(client, peer, UDP, internal, external, peer, 3600, 0, nullptr);
  CALL(client, run);

  pcp_client_destroy(client);

  return 0;
}
