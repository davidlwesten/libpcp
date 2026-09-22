#include "lease.h"

#include "packet.h"

#include <pcp/pcp.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static double rand_in_range(double min, double max) {
  double const scale = (double)rand() / RAND_MAX;
  return min + (scale * (max - min));
}

static void lease_on_timeout(pcp_lease_t *lease);
static void lease_on_send(pcp_lease_t *lease,
                          pcp_transport_size_or_error_t result);

static pcp_lease_retry_result_t initial_retry_advance(pcp_rtx_timer_t *retry,
                                                      time_t /*now*/) {
  double const capped = retry->max_timeout ? fmin(2.0 * retry->timeout.seconds,
                                                  (double)retry->max_timeout)
                                           : 2.0 * retry->timeout.seconds;
  return (pcp_lease_retry_result_t){
      .should_retry = true,
      .timeout = {.seconds = (1.0 + rand_in_range(-0.1, 0.1)) * capped}};
}

static pcp_lease_retry_result_t renewal_retry_advance(pcp_rtx_timer_t *retry,
                                                      time_t now) {
  if (retry->max_duration && now >= retry->max_duration) {
    return (pcp_lease_retry_result_t){.should_retry = false};
  }

  ++retry->count;
  double const factor_start = 1.0 - (1.0 / (double)(1u << retry->count));
  double const factor_width = 1.0 / (double)(1u << (retry->count + 2));
  double const factor = factor_start + factor_width * rand_in_range(0.0, 1.0);
  return (pcp_lease_retry_result_t){
      .should_retry = true,
      .timeout = {.seconds =
                      fmax(factor * (double)(retry->max_duration - now), 4.0)}};
}

static bool lease_retry_expired(pcp_rtx_timer_t const *retry, time_t now) {
  return (retry->max_duration && now >= retry->max_duration) ||
         (retry->max_count && retry->count >= retry->max_count);
}

static pcp_transport_const_view_t lease_packet(pcp_lease_t const *lease) {
  assert(lease->common.request);
  return (pcp_transport_const_view_t){.size = lease->common.request->size,
                                      .data = lease->common.request->data};
}

static void lease_remove_timer(pcp_lease_t *lease) {
  switch (lease->state) {
  case PENDING:
    CALL(&lease->common.transport, remove_timer, lease->pending.rtx.timer);
    break;
  case ACTIVE:
    CALL(&lease->common.transport, remove_timer, lease->active.timer);
    break;
  case RENEWING:
    CALL(&lease->common.transport, remove_timer, lease->renewing.rtx.timer);
    break;
  case FAILED:
    break;
  }
}

static bool lease_arm_timer(pcp_lease_t *lease,
                            pcp_transport_timeout_t timeout) {
  auto const timer_result =
      CALL(&lease->common.transport, add_timer, timeout,
           (pcp_transport_on_timer_t){
               .fn = (pcp_transport_on_timer_callback_t)lease_on_timeout,
               .userdata = lease});
  if (!timer_result.has_value) {
    pcp_failed_state_create(
        lease, (pcp_response_header_or_error_t){.has_value = false,
                                                .error = timer_result.error});
    return false;
  }

  switch (lease->state) {
  case PENDING:
    lease->pending.rtx.timer = timer_result.value;
    break;
  case ACTIVE:
    lease->active.timer = timer_result.value;
    break;
  case RENEWING:
    lease->renewing.rtx.timer = timer_result.value;
    break;
  case FAILED:
    assert(false);
  }
  return true;
}

static bool lease_response_matches(pcp_lease_t const *lease,
                                   pcp_response_t const *response) {
  auto const packet = lease_packet(lease);
  auto const header = pcp_packet_read_request_header(packet);
  if (header.opcode != response->header.opcode) {
    return false;
  }

  auto const request = pcp_packet_read_request(header, packet);
  switch (header.opcode) {
  case MAP:
    return memcmp(&request.map.nonce, &response->map.nonce,
                  sizeof(request.map.nonce)) == 0;
  case PEER:
    return memcmp(&request.peer.nonce, &response->peer.nonce,
                  sizeof(request.peer.nonce)) == 0;
  case ANNOUNCE:
    return true;
  }
  return false;
}

static bool lease_transient_error(pcp_result_t code) {
  switch (code) {
  case NETWORK_FAILURE:
  case NO_RESOURCES:
  case USER_EX_QUOTA:
  case CANNOT_PROVIDE_EXTERNAL:
    return true;
  default:
    return false;
  }
}

static bool lease_retryable_send_error(errno_t error) {
  return error == EAGAIN || error == EWOULDBLOCK || error == EINTR ||
         error == ENOBUFS;
}

static void lease_send(pcp_lease_t *lease) {
  CALL(&lease->common.transport, send, lease_packet(lease),
       (pcp_transport_on_send_t){
           .fn = (pcp_transport_on_send_callback_t)lease_on_send,
           .userdata = lease});
}

static void lease_enter_pending(pcp_lease_t *lease,
                                pcp_transport_timeout_t timeout) {
  auto const common = lease->common;
  *lease =
      (pcp_lease_t){.state = PENDING,
                    .common = common,
                    .pending = {.rtx = {.timeout = timeout,
                                        .max_timeout = 1024,
                                        .max_duration = 0,
                                        .count = 1,
                                        .max_count = 0,
                                        .advance = initial_retry_advance}}};
  lease_arm_timer(lease, timeout);
}

static void lease_enter_active(pcp_lease_t *lease,
                               pcp_response_t const *response) {
  assert(response->header.code == SUCCESS);

  auto const external = response->header.opcode == MAP
                            ? response->map.external
                            : response->peer.external;
  auto const packet = lease_packet(lease);
  auto const header = pcp_packet_read_request_header(packet);
  auto request = pcp_packet_read_request(header, packet);
  if (header.opcode == MAP) {
    request.map.external = external;
  } else {
    request.peer.external = external;
  }
  pcp_packet_write_request(
      (pcp_transport_view_t){.size = lease->common.request->size,
                             .data = lease->common.request->data},
      &request, 0, nullptr);

  time_t const expiration = lease->common.time() + response->header.ttl;
  pcp_transport_timeout_t const delay = {
      .seconds = fmax(rand_in_range(1.0 / 2.0, 5.0 / 8.0) *
                          (double)response->header.ttl,
                      4.0)};
  auto const common = lease->common;
  *lease = (pcp_lease_t){.state = ACTIVE,
                         .common = common,
                         .active = {.header = response->header,
                                    .expiration = expiration}};
  lease_arm_timer(lease, delay);
}

static void lease_enter_renewing(pcp_lease_t *lease) {
  auto const common = lease->common;
  auto const expiration = lease->active.expiration;
  *lease =
      (pcp_lease_t){.state = RENEWING,
                    .common = common,
                    .renewing = {.expiration = expiration,
                                 .rtx = {.timeout = {},
                                         .max_timeout = 0,
                                         .max_duration = expiration,
                                         .count = 1,
                                         .max_count = 0,
                                         .advance = renewal_retry_advance}}};

  time_t const now = lease->common.time();
  auto const next = lease->renewing.rtx.advance(&lease->renewing.rtx, now);
  if (!next.should_retry) {
    pcp_failed_state_create(
        lease, (pcp_response_header_or_error_t){.has_value = false,
                                                .error = ETIMEDOUT});
    return;
  }
  lease->renewing.rtx.timeout = next.timeout;
  lease_arm_timer(lease, next.timeout);
}

static void lease_on_response(pcp_lease_t *lease,
                              pcp_response_t const *response) {
  if (!lease_response_matches(lease, response)) {
    return;
  }

  lease_remove_timer(lease);
  switch (response->header.code) {
  case SUCCESS:
    lease_enter_active(lease, response);
    break;
  default:
    if (lease_transient_error(response->header.code)) {
      pcp_transport_timeout_t const timeout = {
          .seconds = (double)response->header.ttl};
      if (lease->state == PENDING) {
        lease->pending.rtx.timeout = timeout;
        lease->pending.rtx.advance = initial_retry_advance;
        lease_arm_timer(lease, timeout);
      } else {
        lease->renewing.rtx.timeout = timeout;
        lease->renewing.rtx.advance = initial_retry_advance;
        lease->renewing.rtx.count = 1;
        lease_arm_timer(lease, timeout);
      }
    } else {
      pcp_failed_state_create(
          lease, (pcp_response_header_or_error_t){.has_value = true,
                                                  .value = response->header});
    }
    break;
  }
}

static void lease_on_send(pcp_lease_t *lease,
                          pcp_transport_size_or_error_t result) {
  if (result.has_value || lease_retryable_send_error(result.error)) {
    return;
  }

  pcp_failed_state_create(
      lease, (pcp_response_header_or_error_t){.has_value = false,
                                              .error = result.error});
}

static void lease_on_timeout(pcp_lease_t *lease) {
  switch (lease->state) {
  case ACTIVE:
    lease_enter_renewing(lease);
    if (lease->state == RENEWING) {
      lease_send(lease);
    }
    break;
  case PENDING: {
    pcp_rtx_timer_t *retry = &lease->pending.rtx;
    time_t const now = lease->common.time();
    if (lease_retry_expired(retry, now)) {
      pcp_failed_state_create(
          lease, (pcp_response_header_or_error_t){.has_value = false,
                                                  .error = ETIMEDOUT});
      return;
    }
    auto const next = retry->advance(retry, now);
    if (!next.should_retry) {
      pcp_failed_state_create(
          lease, (pcp_response_header_or_error_t){.has_value = false,
                                                  .error = ETIMEDOUT});
      return;
    }
    retry->timeout = next.timeout;
    if (lease_arm_timer(lease, next.timeout)) {
      lease_send(lease);
    }
    break;
  }
  case RENEWING: {
    pcp_rtx_timer_t *retry = &lease->renewing.rtx;
    time_t const now = lease->common.time();
    if (lease_retry_expired(retry, now)) {
      pcp_failed_state_create(
          lease, (pcp_response_header_or_error_t){.has_value = false,
                                                  .error = ETIMEDOUT});
      return;
    }
    auto const next = retry->advance(retry, now);
    if (!next.should_retry) {
      pcp_failed_state_create(
          lease, (pcp_response_header_or_error_t){.has_value = false,
                                                  .error = ETIMEDOUT});
      return;
    }
    retry->timeout = next.timeout;
    if (lease_arm_timer(lease, next.timeout)) {
      lease_send(lease);
    }
    break;
  }
  case FAILED:
    break;
  }
}

void pcp_lease_on_recv(pcp_lease_t *lease, pcp_response_t const *response) {
  if (lease->state != FAILED) {
    lease_on_response(lease, response);
  }
}

void pcp_pending_state_create(pcp_lease_t *lease,
                              pcp_transport_t transport,
                              time_t (*time)(),
                              packet_t *request) {
  assert(lease);
  assert(transport.send);
  assert(transport.add_timer);
  assert(transport.remove_timer);
  assert(time);
  assert(request);
  lease->common = (pcp_lease_common_t){.transport = transport,
                                       .time = time,
                                       .request = request};
  lease_enter_pending(lease,
                      (pcp_transport_timeout_t){
                          .seconds = (1.0 + rand_in_range(-0.1, 0.1)) * 3.0});
}

void pcp_lease_start(pcp_lease_t *lease,
                     pcp_transport_t transport,
                     time_t (*time)(),
                     packet_t *request) {
  pcp_pending_state_create(lease, transport, time, request);
  if (lease->state == PENDING) {
    lease_send(lease);
  }
}

void pcp_failed_state_create(pcp_lease_t *lease,
                             pcp_response_header_or_error_t header) {
  assert(lease);
  lease_remove_timer(lease);
  *lease = (pcp_lease_t){.state = FAILED,
                         .common = lease->common,
                         .failed = {.header = header}};
}
