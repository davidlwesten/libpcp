#include "mapping.h"

#include "client.h"
#include "transport.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void mapping_state_pending_update(pcp_mapping_t *mapping,
                                         pcp_response_t const *response) {
  const_view_t const packet = {.size = mapping->base.request->size,
                               .data = mapping->base.request->data};

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

  CALL(&mapping->pending.rtx.timer, stop);

  if (response->header.code == SUCCESS) {
    *mapping = (pcp_mapping_t){.active = {.base = mapping->pending.base}};
  } else {
    *mapping = (pcp_mapping_t){.error = {.base = mapping->pending.base}};
  }
}

static time_t min_time_t(time_t lhs, time_t rhs) {
  return (lhs < rhs) ? lhs : rhs;
}

static void mapping_state_pending_timeout(pcp_client_internal_t *client,
                                          void *index);

static void mapping_state_pending_on_send(pcp_client_internal_t *client,
                                          void *index,
                                          size_or_error_t result) {
  if (!result.has_value) {
    assert(false);
    return;
  }

  pcp_mapping_t *mapping = client->mappings + (uintptr_t)index;

  time_t const timeout = min_time_t(2 * mapping->pending.rtx.timeout,
                                    mapping->pending.rtx.max_time);
  mapping->pending.rtx.timeout = timeout;
  mapping->pending.rtx.timer = CALL(
      client->transport, add_timer, timeout,
      (transport_on_timer_t){
          .fn = (transport_on_timer_callback_t)mapping_state_pending_timeout,
          .userdata = index});
}

static void mapping_state_pending_timeout(pcp_client_internal_t *client,
                                          void *index) {
  pcp_mapping_t *mapping = client->mappings + (uintptr_t)index;

  time_t const max_duration = mapping->pending.rtx.max_duration;
  time_t const now = time(nullptr);
  if (max_duration && max_duration < (now - mapping->pending.start)) {
    *mapping = (pcp_mapping_t){.error = {.base = mapping->pending.base}};
    return;
  }

  uint8_t const max_count = mapping->pending.rtx.max_count;
  if (max_count && max_count < ++mapping->pending.rtx.count) {
    *mapping = (pcp_mapping_t){.error = {.base = mapping->pending.base}};
    return;
  }

  auto const packet = mapping->pending.base.request;
  CALL(client->transport, send,
       (const_view_t){.size = packet->size, .data = packet->data},
       (transport_on_send_t){
           .fn = (transport_on_send_callback_t)mapping_state_pending_on_send,
           .userdata = index});
}

static void mapping_state_active_update(pcp_mapping_t * /*mapping*/,
                                        pcp_response_t const * /*response*/) {}

static void mapping_state_error_update(pcp_mapping_t * /*state*/,
                                       pcp_response_t const * /*response*/) {}

void pcp_mapping_update(pcp_mapping_t *mapping,
                        pcp_response_t const *response) {
  switch (mapping->base.state) {
  case PENDING:
    mapping_state_pending_update(mapping, response);
    break;
  case ACTIVE:
    mapping_state_active_update(mapping, response);
    break;
  case ERROR:
    mapping_state_error_update(mapping, response);
    break;
  }
}

pcp_mapping_t pcp_mapping_create_pending(transport_t *transport,
                                         packet_t *packet,
                                         size_t index) {
  static constexpr time_t PCP_IRT = 3;
  pcp_rtx_timer_t const timer = {
      .timer =
          CALL(transport, add_timer, PCP_IRT,
               (transport_on_timer_t){.fn = (transport_on_timer_callback_t)
                                          mapping_state_pending_timeout,
                                      .userdata = (void *)(uintptr_t)index}),
      .timeout = PCP_IRT,
      .max_time = PCP_MRT,
      .max_duration = PCP_MRD,
      .count = 1,
      .max_count = PCP_MRC};
  return (pcp_mapping_t){
      .pending = {.base = {.state = PENDING, .request = packet},
                  .start = time(nullptr),
                  .rtx = timer}};
}
