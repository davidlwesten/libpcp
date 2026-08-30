#include "lease.h"

#include "client.h"
#include "packet.h"
#include "transport.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static double rand_in_range(double min, double max) {
  double const scale = (double)rand() / RAND_MAX;
  return min + (scale * (max - min));
}

static void active_state_create(pcp_lease_t *lease,
                                transport_t *transport,
                                pcp_response_t const *response);

static void error_state_create(pcp_lease_t *lease,
                               transport_t *transport,
                               pcp_response_t const *response);

static void renewing_state_create(pcp_lease_t *lease, transport_t *transport);

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
  assert(lease->base.state == FAILED);
}

static void error_state_on_send(pcp_client_internal_t *client,
                                pcp_lease_t *lease,
                                size_or_error_t result) {
  assert(result.has_value); // todo: retry?
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

  transport_timeout_t const ttl = {.seconds = (double)response->header.ttl};
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
  assert(result.has_value); // todo: retry?
  renewing_state_create(lease, client->transport);
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

  transport_timeout_t const delay = {
      .seconds = fmax(rand_in_range(1.0 / 2.0, 5.0 / 8.0) *
                          (double)response->header.ttl,
                      4.0)};
  assert(delay.seconds >= 0);

  auto const timer =
      CALL(transport, add_timer, delay,
           (transport_on_timer_t){
               .fn = (transport_on_timer_callback_t)active_state_on_timeout,
               .userdata = lease});

  socket_address_t const external = (response->header.opcode == MAP)
                                        ? response->map.external
                                        : response->peer.external;

  assert(lease->base.request);

  // need to update suggested external address for renewals
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
                 .timer = timer,
                 .expiration = time(nullptr) + response->header.ttl}};
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

static void renewing_state_on_recv(pcp_lease_t *lease,
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

  CALL(transport, remove_timer, lease->renewing.rtx.timer);

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

static void renewing_state_on_timeout(pcp_client_internal_t *client,
                                      pcp_lease_t *lease);

static void renewing_state_on_send(pcp_client_internal_t *client,
                                   pcp_lease_t *lease,
                                   size_or_error_t result) {
  assert(result.has_value); // todo: retry?
  assert(lease->base.state == RENEWING);

  time_t const now = time(nullptr);
  time_t const remaining = lease->renewing.expiration - now;

  uint8_t const attempt = ++lease->renewing.rtx.count;
  if (lease->renewing.rtx.max_count &&
      lease->renewing.rtx.max_count < attempt) {
    assert(false); // todo: transition to failed
  }

  if (lease->renewing.rtx.max_duration &&
      (lease->renewing.rtx.max_duration < now)) {
    assert(false); // todo: transition to failed
  }

  double const factor_start =
      1.0 - (1.0 / (double)(1u << attempt)); // 1/2, 3/4, 7/8, ...
  double const factor_width =
      1.0 / (double)(1u << (attempt + 2)); // 1/8, 1/16, 1/32, ...
  double const factor = factor_start + factor_width * rand_in_range(0.0, 1.0);

  lease->renewing.rtx.timeout.seconds = fmax(factor * (double)remaining, 4.0);
  lease->renewing.rtx.timer =
      CALL(client->transport, add_timer, lease->renewing.rtx.timeout,
           (transport_on_timer_t){
               .fn = (transport_on_timer_callback_t)renewing_state_on_timeout,
               .userdata = lease});
}

static void renewing_state_on_timeout(pcp_client_internal_t *client,
                                      pcp_lease_t *lease) {
  auto const packet = lease->active.base.request;
  CALL(client->transport, send,
       (const_view_t){.size = packet->size, .data = packet->data},
       (transport_on_send_t){
           .fn = (transport_on_send_callback_t)renewing_state_on_send,
           .userdata = lease});
}

static void renewing_state_create(pcp_lease_t *lease, transport_t *transport) {
  assert(lease->base.state == ACTIVE);

  time_t const now = time(nullptr);
  time_t const expiration = lease->active.expiration;
  time_t const remaining = expiration - now;

  pcp_rtx_timer_t rtx = {.timer = (transport_timer_t)-1,
                         .timeout = {},
                         .max_timeout = 0,
                         .max_duration = expiration,
                         .count =
                             2, // first attempt was in active_state_on_timeout
                         .max_count = 0};

  double const factor_start =
      1.0 - (1.0 / (double)(1u << rtx.count)); // 1/2, 3/4, 7/8, ...
  double const factor_width =
      1.0 / (double)(1u << (rtx.count + 2)); // 1/8, 1/16, 1/32, ...
  double const factor = factor_start + factor_width * rand_in_range(0.0, 1.0);

  rtx.timeout.seconds = fmax(factor * (double)remaining, 4.0);
  rtx.timer =
      CALL(transport, add_timer, rtx.timeout,
           (transport_on_timer_t){
               .fn = (transport_on_timer_callback_t)renewing_state_on_timeout,
               .userdata = lease});

  *lease = (pcp_lease_t){
      .renewing = {.base = {.state = RENEWING, .request = lease->base.request},
                   .expiration = lease->active.expiration,
                   .rtx = rtx}};
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

static void pending_state_on_timeout(pcp_client_internal_t *client,
                                     pcp_lease_t *lease);

static void pending_state_on_send(pcp_client_internal_t *client,
                                  pcp_lease_t *lease,
                                  size_or_error_t result) {
  // todo: retry?
  assert(result.has_value);

  double const doubled = 2.0 * lease->pending.rtx.timeout.seconds;
  double const capped =
      lease->pending.rtx.max_timeout
          ? fmin(doubled, (double)lease->pending.rtx.max_timeout)
          : doubled;
  transport_timeout_t const timeout = {
      .seconds = (1.0 + rand_in_range(-0.1, 0.1)) * capped};
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
  if (max_duration && now < max_duration) {
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
  case RENEWING:
    renewing_state_on_recv(lease, transport, response);
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
  static constexpr time_t PCP_MRT = 1024;
  static constexpr time_t PCP_MRD = 0;
  static constexpr uint8_t PCP_MRC = 0;

  transport_timeout_t const timeout = {
      .seconds = (1 + rand_in_range(-0.1, 0.1)) * PCP_IRT};
  pcp_rtx_timer_t const rtx = {
      .timer = CALL(transport, add_timer, timeout,
                    (transport_on_timer_t){.fn = (transport_on_timer_callback_t)
                                               pending_state_on_timeout,
                                           .userdata = lease}),
      .timeout = timeout,
      .max_timeout = PCP_MRT,
      .max_duration = (PCP_MRD) ? PCP_MRD + time(nullptr) : PCP_MRD,
      .count = 1,
      .max_count = PCP_MRC};
  *lease = (pcp_lease_t){
      .pending = {.base = {.state = PENDING, .request = request}, .rtx = rtx}};
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
