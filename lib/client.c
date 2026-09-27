#include <pcp/pcp.h>

#include "client.h"
#include "lease.h"
#include "map.h"
#include "packet.h"
#include "peer.h"
#include "server.h"

#define _GNU_SOURCE
#include <assert.h>

static time_t pcp_client_time() {
#if !defined(NDEBUG)
  constexpr auto clock_id = CLOCK_THREAD_CPUTIME_ID;
#else
  constexpr auto clock_id = CLOCK_BOOTTIME;
#endif
  struct timespec time = {0};
  assert(0 == clock_gettime(clock_id, &time));
  return time.tv_sec;
}

pcp_nonce_t pcp_client_create_nonce(pcp_client_internal_t * /*client*/) {
  return (pcp_nonce_t){.data = {}};
}

void pcp_client_send(pcp_client_internal_t *client,
                     const pcp_request_t *request,
                     size_t options_count,
                     pcp_option_t const options[options_count]) {
  uint8_t buffer[PCP_PACKET_MAX_SIZE] = {0};
  auto const view = pcp_packet_write_request(
      (pcp_transport_view_t){.size = sizeof(buffer), .data = buffer}, request,
      options_count, options);

  auto const packet = packet_create(client->allocator, view.size, view.data);
  if (packet) {
    auto const lease_begin = client->leases;
    auto const lease_end = lease_begin + PCP_CLIENT_MAX_LEASES;
    optional_lease_t *lease = lease_begin;
    for (; lease != lease_end; ++lease) {
      if (!lease->has_value) {
        break;
      }
    }
    if (lease == lease_end) {
      packet_destroy(packet);
      return;
    }
    lease->has_value = true;
    pcp_lease_start(&lease->value, client->transport, client->time, packet);
  }
}

pcp_client_result_t pcp_client_create(pcp_transport_t transport,
                                      pcp_allocator_t allocator) {
  if (!transport.local || !transport.remote || !transport.send ||
      !transport.add_timer || !transport.remove_timer || !allocator.alloc ||
      !allocator.free) {
    return (pcp_client_result_t){.has_value = false, .error = EINVAL};
  }

  pcp_client_internal_t *internal =
      allocator.alloc(allocator.ctx, sizeof(pcp_client_internal_t));
  if (!internal) {
    return (pcp_client_result_t){.has_value = false, .error = ENOMEM};
  }

  *internal = (pcp_client_internal_t){.server = {},
                                      .transport = transport,
                                      .allocator = allocator,
                                      .leases = {},
                                      .time = pcp_client_time};
  return (pcp_client_result_t){
      .has_value = true,
      .value = {.data = internal,
                .map = (void (*)(void *, pcp_protocol_t, port_t,
                                 socket_address_t, time_t, size_t,
                                 pcp_option_t const[]))pcp_client_map_request,
                .peer =
                    (void (*)(void *, pcp_protocol_t, port_t, socket_address_t,
                              socket_address_t, time_t, size_t,
                              pcp_option_t const[]))pcp_client_peer_request}};
}

void pcp_client_destroy(pcp_client_t *client) {
  pcp_client_internal_t *internal = client->data;
  auto const begin = internal->leases;
  auto const end = begin + PCP_CLIENT_MAX_LEASES;
  for (auto lease = begin; lease != end; ++lease) {
    if (lease->has_value) {
      pcp_lease_destroy(&lease->value);
    }
  }
  internal->allocator.free(internal->allocator.ctx, internal);
}
