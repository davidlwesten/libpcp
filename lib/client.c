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

static void pcp_client_shutdown(void *data,
                                time_t timeout_seconds,
                                pcp_client_on_shutdown_t callback,
                                void *userdata);

pcp_nonce_t pcp_client_create_nonce(pcp_client_internal_t *) {
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
      packet_destroy(packet, client->allocator);
      return;
    }
    lease->has_value = true;
    pcp_lease_start(&lease->value, client, packet);
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
                                      .time = pcp_client_time,
                                      .shutdown_error = {.has_error = false},
                                      .shutdown_callback = nullptr,
                                      .shutdown_userdata = nullptr};
  return (pcp_client_result_t){
      .has_value = true,
      .value = {.data = internal,
                .map = (void (*)(void *, pcp_protocol_t, port_t,
                                 socket_address_t, time_t, size_t,
                                 pcp_option_t const[]))pcp_client_map_request,
                .peer =
                    (void (*)(void *, pcp_protocol_t, port_t, socket_address_t,
                              socket_address_t, time_t, size_t,
                              pcp_option_t const[]))pcp_client_peer_request,
                .shutdown = pcp_client_shutdown}};
}

static void pcp_client_shutdown_finish(pcp_client_internal_t *client) {
  if (!client->shutdown_callback) {
    return;
  }

  for (auto lease = client->leases;
       lease != client->leases + PCP_CLIENT_MAX_LEASES; ++lease) {
    if (lease->has_value && lease->value.state != SHUTDOWN) {
      return;
    }
  }

  pcp_client_on_shutdown_t callback = client->shutdown_callback;
  void *userdata = client->shutdown_userdata;
  pcp_optional_error_t const result = client->shutdown_error;
  client->shutdown_callback = nullptr;
  client->shutdown_userdata = nullptr;

  auto const begin = client->leases;
  auto const end = begin + PCP_CLIENT_MAX_LEASES;
  for (auto lease = begin; lease != end; ++lease) {
    if (lease->has_value) {
      pcp_lease_destroy(&lease->value);
      lease->has_value = false;
    }
  }

  callback(userdata, result);
}

void pcp_client_on_lease_shutdown_complete(void *userdata,
                                           pcp_optional_error_t result) {
  pcp_client_internal_t *client = userdata;
  assert(client);
  if (result.has_error && !client->shutdown_error.has_error) {
    client->shutdown_error = result;
  }
  pcp_client_shutdown_finish(client);
}

static void pcp_client_shutdown(void *data,
                                time_t timeout_seconds,
                                pcp_client_on_shutdown_t callback,
                                void *userdata) {
  assert(data);
  assert(callback);
  assert(timeout_seconds >= 0);

  pcp_client_internal_t *internal = data;

  auto const begin = internal->leases;
  auto const end = begin + PCP_CLIENT_MAX_LEASES;
  for (auto lease = begin; lease != end; ++lease) {
    if (lease->has_value) {
      pcp_lease_shutdown(&lease->value, timeout_seconds,
                         pcp_client_on_lease_shutdown_complete, internal);
    }
  }

  internal->shutdown_callback = callback;
  internal->shutdown_userdata = userdata;
  pcp_client_shutdown_finish(internal);
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
