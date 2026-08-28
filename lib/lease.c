#include "lease.h"

#include "client.h"
#include "packet.h"
#include "transport.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void active_lease_timeout(pcp_client_internal_t *client,
                                 pcp_lease_t *lease);

static void active_lease_create(pcp_lease_t *lease,
                                transport_t *transport,
                                pcp_response_t const *response) {
  assert(response->header.code == SUCCESS);
  assert(response->header.opcode == MAP || response->header.opcode == PEER);

  // todo: random ttl between 1/2 and 5/8 of lifetime
  time_t const ttl = response->header.ttl / 2;
  assert(ttl >= 0);

  auto const timer =
      CALL(transport, add_timer, ttl,
           (transport_on_timer_t){
               .fn = (transport_on_timer_callback_t)active_lease_timeout,
               .userdata = lease});

  socket_address_t const external = (response->header.opcode == MAP)
                                        ? response->map.external
                                        : response->peer.external;

  // need to update suggested external address on renewals
  const_view_t const const_view = {.size = lease->base.request->size,
                                   .data = lease->base.request->data};
  pcp_request_header_t const header =
      pcp_packet_read_request_header(const_view);
  pcp_request_t request = pcp_packet_read_request(header, const_view);

  packet_destroy(lease->base.request);

  if (header.opcode == MAP) {
    request.map.external = external;
  } else if (header.opcode == PEER) {
    request.peer.external = external;
  }

  *lease = (pcp_lease_t){
      .active = {.base = {.state = ACTIVE,
                          .request = pcp_packet_write_request(&request)},
                 .timer = timer}};
}

static void pending_lease_update(pcp_lease_t *lease,
                                 transport_t *transport,
                                 pcp_response_t const *response) {
  const_view_t const packet = {.size = lease->base.request->size,
                               .data = lease->base.request->data};

  pcp_request_header_t const header = pcp_packet_read_request_header(packet);
  if (header.opcode != response->header.opcode) {
    return;
  }

  pcp_request_t const request = pcp_packet_read_request(header, packet);
  if (header.opcode == MAP && memcmp(&request.map.nonce, &response->map.nonce,
                                     sizeof(request.map.nonce)) != 0) {
    return;
  }

  if (header.opcode == PEER &&
      memcmp(&request.peer.nonce, &response->peer.nonce,
             sizeof(request.peer.nonce)) != 0) {
    return;
  }

  CALL(transport, remove_timer, lease->pending.rtx.timer);

  if (response->header.code == SUCCESS) {
    active_lease_create(lease, transport, response);
  } else {
    *lease = (pcp_lease_t){.error = {.base = lease->pending.base}};
  }
}

static time_t min_time_t(time_t lhs, time_t rhs) {
  return (lhs < rhs) ? lhs : rhs;
}

static void pending_lease_timeout(pcp_client_internal_t *client,
                                  pcp_lease_t *lease);

static void pending_lease_on_send(pcp_client_internal_t *client,
                                  pcp_lease_t *lease,
                                  size_or_error_t result) {
  assert(result.has_value);

  time_t const timeout =
      min_time_t(2 * lease->pending.rtx.timeout, lease->pending.rtx.max_time);
  lease->pending.rtx.timeout = timeout;
  lease->pending.rtx.timer =
      CALL(client->transport, add_timer, timeout,
           (transport_on_timer_t){
               .fn = (transport_on_timer_callback_t)pending_lease_timeout,
               .userdata = lease});
}

static void pending_lease_timeout(pcp_client_internal_t *client,
                                  pcp_lease_t *lease) {
  time_t const max_duration = lease->pending.rtx.max_duration;
  time_t const now = time(nullptr);
  if (max_duration && max_duration < (now - lease->pending.start)) {
    *lease = (pcp_lease_t){.error = {.base = lease->pending.base}};
    return;
  }

  uint8_t const max_count = lease->pending.rtx.max_count;
  if (max_count && max_count < ++lease->pending.rtx.count) {
    *lease = (pcp_lease_t){.error = {.base = lease->pending.base}};
    return;
  }

  auto const packet = lease->pending.base.request;
  CALL(client->transport, send,
       (const_view_t){.size = packet->size, .data = packet->data},
       (transport_on_send_t){
           .fn = (transport_on_send_callback_t)pending_lease_on_send,
           .userdata = lease});
}

static void active_lease_update(pcp_lease_t * /*lease*/,
                                transport_t * /*transport*/,
                                pcp_response_t const * /*response*/) {}

static void active_lease_on_send(pcp_client_internal_t *client,
                                 pcp_lease_t *lease,
                                 size_or_error_t result) {
  assert(result.has_value);

  pcp_pending_lease_create(lease, client->transport,
                           lease->active.base.request);
}

static void active_lease_timeout(pcp_client_internal_t *client,
                                 pcp_lease_t *lease) {
  auto const packet = lease->active.base.request;
  CALL(client->transport, send,
       (const_view_t){.size = packet->size, .data = packet->data},
       (transport_on_send_t){
           .fn = (transport_on_send_callback_t)active_lease_on_send,
           .userdata = lease});
}

static void error_lease_update(pcp_lease_t * /*state*/,
                               transport_t * /*transport*/,
                               pcp_response_t const * /*response*/) {}

static void failed_lease_update(pcp_lease_t * /*state*/,
                                transport_t * /*transport*/,
                                pcp_response_t const * /*response*/) {}

void pcp_lease_update(pcp_lease_t *lease,
                      transport_t *transport,
                      pcp_response_t const *response) {
  switch (lease->base.state) {
  case PENDING:
    pending_lease_update(lease, transport, response);
    break;
  case ACTIVE:
    active_lease_update(lease, transport, response);
    break;
  case ERROR:
    error_lease_update(lease, transport, response);
    break;
  case FAILED:
    failed_lease_update(lease, transport, response);
    break;
  }
}

void pcp_pending_lease_create(pcp_lease_t *lease,
                              transport_t *transport,
                              packet_t *request) {
  assert(lease);
  assert(transport);
  assert(request);

  static constexpr time_t PCP_IRT = 3;
  pcp_rtx_timer_t const timer = {
      .timer =
          CALL(transport, add_timer, PCP_IRT,
               (transport_on_timer_t){
                   .fn = (transport_on_timer_callback_t)pending_lease_timeout,
                   .userdata = lease}),
      .timeout = PCP_IRT,
      .max_time = PCP_MRT,
      .max_duration = PCP_MRD,
      .count = 1,
      .max_count = PCP_MRC};
  *lease =
      (pcp_lease_t){.pending = {.base = {.state = PENDING, .request = request},
                                .start = time(nullptr),
                                .rtx = timer}};
}
