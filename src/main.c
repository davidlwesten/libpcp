#include <pcp/pcp.h>

#include <stdio.h>

static void socket_address_print(char const *prefix,
                                 const socket_address_t *addr) {
  char string[1024];
  if (socket_address_ntop(sizeof(string), string, addr)) {
    printf("%s: %s\n", prefix, string);
  } else {
    printf("%s: <error>\n", prefix);
  }
}

static void mapping_callback(pcp_mapping_or_error_t result) {
  if (result.code == SUCCESS) {
    socket_address_print("mapped address is", &result.value.external);
  } else {
    fprintf(stderr, "mapping failed: %s\n", pcp_strerror(result.code));
  }
}

int main() {
  pcp_client_t *client = pcp_client_create("127.0.0.1");
  if (!client) {
    fprintf(stderr, "pcp_client_create failed!\n");
    return 1;
  }

  socket_address_print("client is", &client->local);
  socket_address_print("server is", &client->remote);

  constexpr port_t internal = 12000;
  constexpr socket_address_t external = {.ip = ip_address_any_v6,
                                         .port = 12000};
  const socket_address_t peer = {.ip = ip_address_from_octets(192, 168, 8, 2),
                                 .port = 12000};

  CALL(client, peer, UDP, internal, external, peer, 3600, 0, nullptr,
       mapping_callback);
  pcp_client_destroy(client);

  return 0;
}
