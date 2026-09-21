#include <pcp/option.h>
#include <pcp/pcp.h>

#include "transport_socket.h"

int main() {
  auto const transport_socket_result = transport_socket_connect("127.0.0.1");
  if (!transport_socket_result.has_value) {
    return 1;
  }
  transport_socket_t transport_socket = transport_socket_result.value;
  pcp_transport_t transport = transport_socket_interface(&transport_socket);

  auto const client_result = pcp_client_create(transport);
  if (!client_result.has_value) {
    CALL(&transport, close);
    return 1;
  }
  pcp_client_t client = client_result.value;
  constexpr port_t internal = 12000;
  constexpr socket_address_t external = {.ip = ip_address_any_v6,
                                         .port = 12000};
  [[maybe_unused]] const socket_address_t peer = {
      .ip = ip_address_from_octets(192, 168, 8, 2),
      .port = 12000};

  pcp_option_t options[] = {pcp_option_create_prefer_failure(false)};
  constexpr size_t options_count = sizeof(options) / sizeof(options[0]);

  CALL(&client, map, UDP, internal, external, 15, options_count, options);
  // CALL(client, peer, UDP, internal, external, peer, 90, 0, nullptr);
  CALL(&client, run);

  pcp_client_destroy(&client);
  CALL(&transport, close);

  return 0;
}
