#include <pcp/pcp.h>

#include "client.h"
#include "lease.h"
#include "map.h"
#include "packet.h"
#include "peer.h"
#include "server.h"

#define _GNU_SOURCE
#include <assert.h>
#include <malloc.h>

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

static void pcp_client_run(pcp_client_internal_t *internal) {
  CALL(&internal->transport, run, internal);
}

pcp_nonce_t pcp_client_create_nonce(pcp_client_internal_t * /*client*/) {
  return (pcp_nonce_t){.data = {}};
}

static void pcp_client_on_send(packet_t *request,
                               pcp_transport_size_or_error_t result) {
  auto const client = request->client;
  if (!result.has_value &&
      (result.error == EAGAIN || result.error == EWOULDBLOCK ||
       result.error == EINTR || result.error == ENOBUFS)) {
    CALL(&client->transport, send,
         (pcp_transport_const_view_t){.size = request->size, .data = request->data},
         (pcp_transport_on_send_t){
             .fn = (pcp_transport_on_send_callback_t)pcp_client_on_send,
             .userdata = request});
    return;
  }

  auto const begin = client->leases;
  auto const end = begin + PCP_CLIENT_MAX_LEASES;
  auto iter = begin;
  for (; iter != end; ++iter) {
    if (!iter->has_value) {
      break;
    }
  }

  assert(iter != end);
  iter->has_value = true;

  if (!result.has_value) {
    pcp_failed_state_create(
        &iter->value,
        (pcp_response_header_or_error_t){.has_value = false,
                                         .error = result.error},
        request);
  } else {
    assert(result.value == request->size);
    pcp_pending_state_create(&iter->value, client, request);
  }
}

void pcp_client_send(pcp_client_internal_t *client,
                     const pcp_request_t *request,
                     size_t options_count,
                     pcp_option_t const options[options_count]) {
  uint8_t buffer[PCP_PACKET_MAX_SIZE] = {0};
  auto const view =
      pcp_packet_write_request((pcp_transport_view_t){.size = sizeof(buffer), .data = buffer},
                               request, options_count, options);

  auto const packet = packet_create(view.size, view.data);
  if (packet) {
    packet->client = client;
    CALL(&client->transport, send,
         (pcp_transport_const_view_t){.size = packet->size, .data = packet->data},
         (pcp_transport_on_send_t){
             .fn = (pcp_transport_on_send_callback_t)pcp_client_on_send,
             .userdata = packet});
  }
}

pcp_client_result_t pcp_client_create(pcp_transport_t transport) {
  if (!transport.local || !transport.remote || !transport.send ||
      !transport.add_timer || !transport.remove_timer || !transport.run) {
    return (pcp_client_result_t){.has_value = false, .error = EINVAL};
  }

  pcp_client_internal_t *internal = malloc(sizeof(pcp_client_internal_t));
  if (!internal) {
    free(internal);
    return (pcp_client_result_t){.has_value = false, .error = ENOMEM};
  }

  *internal = (pcp_client_internal_t){.server = {},
                                      .transport = transport,
                                      .leases = {},
                                      .time = pcp_client_time};
  return (pcp_client_result_t){
      .has_value = true,
      .value = {.data = internal,
                .map = (void (*)(void *, pcp_protocol_t, port_t,
                                 socket_address_t, time_t, size_t,
                                 pcp_option_t const[]))pcp_client_map_request,
                .peer = (void (*)(void *, pcp_protocol_t, port_t,
                                  socket_address_t, socket_address_t, time_t,
                                  size_t, pcp_option_t const[]))
                    pcp_client_peer_request,
                .run = (void (*)(void *))pcp_client_run}};
}

void pcp_client_destroy(pcp_client_t *client) {
  free(client->data);
}
