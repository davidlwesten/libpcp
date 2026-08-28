#include "lease.h"

#include "client.h"
#include "transport.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

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
    *lease = (pcp_lease_t){.active = {.base = lease->pending.base}};
  } else {
    *lease = (pcp_lease_t){.error = {.base = lease->pending.base}};
  }
}

static time_t min_time_t(time_t lhs, time_t rhs) {
  return (lhs < rhs) ? lhs : rhs;
}

static void pending_lease_timeout(pcp_client_internal_t *client, void *index);

static void pending_lease_on_send(pcp_client_internal_t *client,
                                  void *index,
                                  size_or_error_t result) {
  if (!result.has_value) {
    return;
  }

  pcp_lease_t *lease = client->leases + (uintptr_t)index;

  time_t const timeout =
      min_time_t(2 * lease->pending.rtx.timeout, lease->pending.rtx.max_time);
  lease->pending.rtx.timeout = timeout;
  lease->pending.rtx.timer =
      CALL(client->transport, add_timer, timeout,
           (transport_on_timer_t){
               .fn = (transport_on_timer_callback_t)pending_lease_timeout,
               .userdata = index});
}

static void pending_lease_timeout(pcp_client_internal_t *client, void *index) {
  pcp_lease_t *lease = client->leases + (uintptr_t)index;

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
           .userdata = index});
}

static void active_lease_update(pcp_lease_t * /*lease*/,
                                transport_t * /*transport*/,
                                pcp_response_t const * /*response*/) {}

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

pcp_lease_t pcp_pending_lease_create(transport_t *transport,
                                     packet_t *packet,
                                     size_t index) {
  static constexpr time_t PCP_IRT = 3;
  pcp_rtx_timer_t const timer = {
      .timer =
          CALL(transport, add_timer, PCP_IRT,
               (transport_on_timer_t){
                   .fn = (transport_on_timer_callback_t)pending_lease_timeout,
                   .userdata = (void *)(uintptr_t)index}),
      .timeout = PCP_IRT,
      .max_time = PCP_MRT,
      .max_duration = PCP_MRD,
      .count = 1,
      .max_count = PCP_MRC};
  return (pcp_lease_t){
      .pending = {.base = {.state = PENDING, .request = packet},
                  .start = time(nullptr),
                  .rtx = timer}};
}
