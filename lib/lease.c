#include "lease.h"

#include "client.h"
#include "packet.h"
#include "transport.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void active_state_create(pcp_lease_t *lease,
                                transport_t *transport,
                                pcp_response_t const *response);

static void error_state_create(pcp_lease_t *lease,
                               transport_t *transport,
                               pcp_response_t const *response);

static void failed_state_on_recv(pcp_lease_t *lease,
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

  switch (response->header.code) {
  case SUCCESS:
    active_state_create(lease, transport, response);
    break;
  case NETWORK_FAILURE:
  case NO_RESOURCES:
  case USER_EX_QUOTA:
  case CANNOT_PROVIDE_EXTERNAL:
    error_state_create(lease, transport, response);
    break;
  default:
    break;
  }
}

static void error_state_on_send(pcp_client_internal_t *client,
                                pcp_lease_t *lease,
                                size_or_error_t result) {
  assert(result.has_value);
  pcp_pending_state_create(lease, client->transport, lease->error.base.request);
}

static void error_state_on_timeout(transport_t *transport, pcp_lease_t *lease) {
  const_view_t const packet = {.size = lease->error.base.request->size,
                               .data = lease->error.base.request->data};
  CALL(transport, send, packet,
       (transport_on_send_t){
           .fn = (transport_on_send_callback_t)error_state_on_send,
           .userdata = lease});
}

static void error_state_create(pcp_lease_t *lease,
                               transport_t *transport,
                               pcp_response_t const *response) {
  assert(response->header.code == NETWORK_FAILURE ||
         response->header.code == NO_RESOURCES ||
         response->header.code == USER_EX_QUOTA ||
         response->header.code == CANNOT_PROVIDE_EXTERNAL);

  transport_timeout_t const ttl = {.seconds = response->header.ttl};
  transport_timer_t const timer =
      CALL(transport, add_timer, ttl,
           (transport_on_timer_t){
               .fn = (transport_on_timer_callback_t)error_state_on_timeout,
               .userdata = lease});

  assert(lease->base.request);
  *lease = (pcp_lease_t){
      .error = {.base = {.state = ERROR, .request = lease->base.request},
                .header = response->header,
                .timer = timer}};
}

static void error_state_on_recv(pcp_lease_t *lease,
                                transport_t *transport,
                                pcp_response_t const *response) {
  assert(lease->base.request);

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

  CALL(transport, remove_timer, lease->error.timer);

  switch (response->header.code) {
  case SUCCESS:
    active_state_create(lease, transport, response);
    break;
  case NETWORK_FAILURE:
  case NO_RESOURCES:
  case USER_EX_QUOTA:
  case CANNOT_PROVIDE_EXTERNAL:
    error_state_create(lease, transport, response);
    break;
  default:
    pcp_failed_state_create(
        lease,
        (pcp_response_header_or_error_t){.has_value = true,
                                         .value = response->header},
        lease->base.request);
    break;
  }
}

static void active_state_on_send(pcp_client_internal_t *client,
                                 pcp_lease_t *lease,
                                 size_or_error_t result) {
  assert(result.has_value);

  pcp_pending_state_create(lease, client->transport,
                           lease->active.base.request);
}

static void active_state_on_timeout(pcp_client_internal_t *client,
                                    pcp_lease_t *lease) {
  auto const packet = lease->active.base.request;
  CALL(client->transport, send,
       (const_view_t){.size = packet->size, .data = packet->data},
       (transport_on_send_t){
           .fn = (transport_on_send_callback_t)active_state_on_send,
           .userdata = lease});
}

static void active_state_create(pcp_lease_t *lease,
                                transport_t *transport,
                                pcp_response_t const *response) {
  assert(response->header.code == SUCCESS);
  assert(response->header.opcode == MAP || response->header.opcode == PEER);

  // todo: random ttl between 1/2 and 5/8 of lifetime
  transport_timeout_t const ttl = {.seconds = response->header.ttl / 2};
  assert(ttl.seconds >= 0);

  auto const timer =
      CALL(transport, add_timer, ttl,
           (transport_on_timer_t){
               .fn = (transport_on_timer_callback_t)active_state_on_timeout,
               .userdata = lease});

  socket_address_t const external = (response->header.opcode == MAP)
                                        ? response->map.external
                                        : response->peer.external;

  assert(lease->base.request);

  // need to update suggested external address on renewals
  const_view_t const const_view = {.size = lease->base.request->size,
                                   .data = lease->base.request->data};
  pcp_request_header_t const header =
      pcp_packet_read_request_header(const_view);
  pcp_request_t request = pcp_packet_read_request(header, const_view);

  if (header.opcode == MAP) {
    request.map.external = external;
  } else if (header.opcode == PEER) {
    request.peer.external = external;
  }

  pcp_packet_write_request((view_t){.size = lease->base.request->size,
                                    .data = lease->base.request->data},
                           &request, 0, nullptr);

  *lease = (pcp_lease_t){
      .active = {.base = {.state = ACTIVE, .request = lease->base.request},
                 .header = response->header,
                 .timer = timer}};
}

static void active_state_on_recv(pcp_lease_t *lease,
                                 transport_t *transport,
                                 pcp_response_t const *response) {
  assert(lease->base.request);
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

  CALL(transport, remove_timer, lease->active.timer);

  switch (response->header.code) {
  case SUCCESS:
    active_state_create(lease, transport, response);
    break;
  case NETWORK_FAILURE:
  case NO_RESOURCES:
  case USER_EX_QUOTA:
  case CANNOT_PROVIDE_EXTERNAL:
    error_state_create(lease, transport, response);
    break;
  default:
    pcp_failed_state_create(
        lease,
        (pcp_response_header_or_error_t){.has_value = true,
                                         .value = response->header},
        lease->base.request);
    break;
  }
}

static void pending_state_on_recv(pcp_lease_t *lease,
                                  transport_t *transport,
                                  pcp_response_t const *response) {
  assert(lease->base.request);
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

  switch (response->header.code) {
  case SUCCESS:
    active_state_create(lease, transport, response);
    break;
  case NETWORK_FAILURE:
  case NO_RESOURCES:
  case USER_EX_QUOTA:
  case CANNOT_PROVIDE_EXTERNAL:
    error_state_create(lease, transport, response);
    break;
  default:
    pcp_failed_state_create(
        lease,
        (pcp_response_header_or_error_t){.has_value = true,
                                         .value = response->header},
        lease->base.request);
    break;
  }
}

static time_t min_time_t(time_t lhs, time_t rhs) {
  return (lhs < rhs) ? lhs : rhs;
}

static void pending_state_on_timeout(pcp_client_internal_t *client,
                                     pcp_lease_t *lease);

static void pending_state_on_send(pcp_client_internal_t *client,
                                  pcp_lease_t *lease,
                                  size_or_error_t result) {
  assert(result.has_value);

  // todo: randomization factor
  transport_timeout_t const timeout = {
      .seconds = min_time_t(2 * lease->pending.rtx.timeout.seconds,
                            lease->pending.rtx.max_time)};
  lease->pending.rtx.timeout = timeout;
  lease->pending.rtx.timer =
      CALL(client->transport, add_timer, timeout,
           (transport_on_timer_t){
               .fn = (transport_on_timer_callback_t)pending_state_on_timeout,
               .userdata = lease});
}

static void pending_state_on_timeout(pcp_client_internal_t *client,
                                     pcp_lease_t *lease) {
  assert(client);
  assert(lease);
  assert(lease->base.request);

  time_t const max_duration = lease->pending.rtx.max_duration;
  time_t const now = time(nullptr);
  if (max_duration && max_duration < (now - lease->pending.start)) {
    pcp_failed_state_create(
        lease,
        (pcp_response_header_or_error_t){.has_value = false,
                                         .error = ETIMEDOUT},
        lease->base.request);
    return;
  }

  uint8_t const max_count = lease->pending.rtx.max_count;
  if (max_count && max_count < ++lease->pending.rtx.count) {
    pcp_failed_state_create(
        lease,
        (pcp_response_header_or_error_t){.has_value = false,
                                         .error = ETIMEDOUT},
        lease->base.request);
    return;
  }

  auto const packet = lease->pending.base.request;
  CALL(client->transport, send,
       (const_view_t){.size = packet->size, .data = packet->data},
       (transport_on_send_t){
           .fn = (transport_on_send_callback_t)pending_state_on_send,
           .userdata = lease});
}

void pcp_lease_on_recv(pcp_lease_t *lease,
                       transport_t *transport,
                       pcp_response_t const *response) {
  switch (lease->base.state) {
  case PENDING:
    pending_state_on_recv(lease, transport, response);
    break;
  case ACTIVE:
    active_state_on_recv(lease, transport, response);
    break;
  case ERROR:
    error_state_on_recv(lease, transport, response);
    break;
  case FAILED:
    failed_state_on_recv(lease, transport, response);
    break;
  }
}

void pcp_pending_state_create(pcp_lease_t *lease,
                              transport_t *transport,
                              packet_t *request) {
  assert(lease);
  assert(transport);
  assert(request);

  static constexpr time_t PCP_IRT = 3;
  pcp_rtx_timer_t const timer = {
      .timer = CALL(
          transport, add_timer, (transport_timeout_t){.seconds = PCP_IRT},
          (transport_on_timer_t){
              .fn = (transport_on_timer_callback_t)pending_state_on_timeout,
              .userdata = lease}),
      .timeout = {.seconds = PCP_IRT},
      .max_time = PCP_MRT,
      .max_duration = PCP_MRD,
      .count = 1,
      .max_count = PCP_MRC};
  *lease =
      (pcp_lease_t){.pending = {.base = {.state = PENDING, .request = request},
                                .start = time(nullptr),
                                .rtx = timer}};
}

void pcp_failed_state_create(pcp_lease_t *lease,
                             pcp_response_header_or_error_t header,
                             packet_t *request) {
  assert(!header.has_value ||
         (header.has_value && !(header.value.code == NETWORK_FAILURE ||
                                header.value.code == NO_RESOURCES ||
                                header.value.code == USER_EX_QUOTA ||
                                header.value.code == CANNOT_PROVIDE_EXTERNAL)));
  *lease =
      (pcp_lease_t){.failed = {.base = {.state = FAILED, .request = request},
                               .header = header}};
}
