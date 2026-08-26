#include "server.h"

#include "array.h"
#include "client.h"
#include "map.h"
#include "mapping.h"
#include "packet.h"
#include "peer.h"
#include "response.h"

#include <assert.h>

static bool pcp_result_valid(pcp_result_t code) {
  switch (code) {
  case SUCCESS:
  case UNSUPP_VERSION:
  case NOT_AUTHORIZED:
  case MALFORMED_REQUEST:
  case UNSUPP_OPCODE:
  case UNSUPP_OPTION:
  case MALFORMED_OPTION:
  case NETWORK_FAILURE:
  case NO_RESOURCES:
  case UNSUPP_PROTOCOL:
  case USER_EX_QUOTA:
  case CANNOT_PROVIDE_EXTERNAL:
  case ADDRESS_MISMATCH:
  case EXCESSIVE_REMOTE_PEERS:
    return true;
  }
  return false;
}

static bool pcp_protocol_valid(pcp_protocol_t protocol) {
  switch (protocol) {
  case ALL:
  case UDP:
  case TCP:
    return true;
  }
  return false;
}

typedef struct pcp_response_or_error {
  bool has_value;
  union {
    pcp_response_t value;
    errno_t error;
  };
} pcp_response_or_error_t;

static pcp_response_or_error_t
read_and_validate_announce_response(pcp_response_header_t header) {
  return (pcp_response_or_error_t){.has_value = true,
                                   .value = {.announce = {.header = header}}};
}

static pcp_response_or_error_t
read_and_validate_map_response(pcp_response_header_t header,
                               const_view_t packet) {
  pcp_response_t const response = {
      .map = pcp_packet_body_read_map_response(header, packet)};

  if (!pcp_protocol_valid(response.map.protocol)) {
    return (pcp_response_or_error_t){.has_value = false, .error = ENOTSUP};
  }

  return (pcp_response_or_error_t){.has_value = true, .value = response};
}

static pcp_response_or_error_t
read_and_validate_peer_response(pcp_response_header_t header,
                                const_view_t packet) {
  pcp_response_t const response = {
      .peer = pcp_packet_body_read_peer_response(header, packet)};

  if (!pcp_protocol_valid(response.peer.protocol)) {
    return (pcp_response_or_error_t){.has_value = false, .error = ENOTSUP};
  }

  return (pcp_response_or_error_t){.has_value = true, .value = response};
}

typedef struct optional_pcp_response_header {
  bool has_value;
  pcp_response_header_t value;
} optional_pcp_response_header_t;

static optional_pcp_response_header_t
read_and_validate_response_header(const_view_t packet) {
  if (packet.size < 4) {
    // missing header
    return (optional_pcp_response_header_t){.has_value = false};
  }

  if (packet.data[0] & 0x80) {
    // not a response
    return (optional_pcp_response_header_t){.has_value = false};
  }

  auto const header = pcp_packet_read_response_header(packet);

  if (!pcp_result_valid(header.code)) {
    return (optional_pcp_response_header_t){.has_value = false};
  }

  if (packet.size < 24 || packet.size > PCP_PACKET_MAX_SIZE) {
    // invalid response size
    return (optional_pcp_response_header_t){.has_value = false};
  }

  return (optional_pcp_response_header_t){.has_value = true, .value = header};
}

void pcp_client_recv(pcp_client_internal_t *client,
                     void * /*userdata*/,
                     const_view_t packet) {
  auto const header = read_and_validate_response_header(packet);
  if (!header.has_value) {
    return;
  }

  pcp_response_or_error_t result;
  switch (header.value.opcode) {
  case ANNOUNCE:
    result = read_and_validate_announce_response(header.value);
    break;
  case MAP:
    result = read_and_validate_map_response(header.value, packet);
    break;
  case PEER:
    result = read_and_validate_peer_response(header.value, packet);
    break;
  }

  if (!result.has_value) {
    return;
  }

  auto const begin = client->mappings;
  auto const end = begin + array_size(client->mappings);
  for (auto mapping = begin; mapping != end; ++mapping) {
    pcp_mapping_update(mapping, &result.value);
  }
}
