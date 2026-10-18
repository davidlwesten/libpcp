#include "lease.h"

#include "client.h"
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
static void lease_on_shutdown_timeout(void *userdata);
static void lease_shutdown_complete(pcp_lease_t *lease);
static void lease_send_delete(pcp_lease_t *lease);

static pcp_lease_retry_result_t initial_retry_advance(pcp_rtx_timer_t *retry,
                                                      time_t) {
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
  case SHUTTING_DOWN:
  case SHUTDOWN:
    break;
  }
}

void pcp_lease_cancel_shutdown_timer(pcp_lease_t *lease) {
  if (lease->common.shutdown_response_timer_active) {
    CALL(&lease->common.transport, remove_timer, lease->common.shutdown_timer);
  }
  lease->common.shutdown_response_timer_active = false;
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
  case SHUTTING_DOWN:
  case SHUTDOWN:
    assert(false);
  }
  return true;
}

bool pcp_lease_response_matches(pcp_lease_t const *lease,
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
  ++lease->common.pending_sends;
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
  *lease = (pcp_lease_t){
      .state = ACTIVE,
      .common = common,
      .active = {.header = response->header, .expiration = expiration}};
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
  if (!pcp_lease_response_matches(lease, response)) {
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
  assert(lease->common.pending_sends > 0);
  --lease->common.pending_sends;

  switch (lease->state) {
  case PENDING:
  case RENEWING:
    if (!result.has_value && !lease_retryable_send_error(result.error)) {
      pcp_failed_state_create(
          lease, (pcp_response_header_or_error_t){.has_value = false,
                                                  .error = result.error});
    }
    break;
  case ACTIVE:
  case FAILED:
    break;
  case SHUTTING_DOWN:
    if (lease->common.shutdown_delete_sent) {
      if (!result.has_value && !lease->common.shutdown_error.has_error) {
        lease->common.shutdown_error =
            (pcp_optional_error_t){.has_error = true, .error = result.error};
      }
      if (!lease->common.shutdown_response_timer_active ||
          lease->common.shutdown_response_received || !result.has_value) {
        lease_shutdown_complete(lease);
      }
    } else if (lease->common.pending_sends == 0) {
      lease_send_delete(lease);
    }
    break;
  case SHUTDOWN:
    assert(false);
    break;
  }
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
  case SHUTTING_DOWN:
  case SHUTDOWN:
    break;
  }
}

void pcp_lease_on_recv(pcp_lease_t *lease, pcp_response_t const *response) {
  if (lease->state == PENDING || lease->state == ACTIVE ||
      lease->state == RENEWING) {
    lease_on_response(lease, response);
  }
}

void pcp_lease_destroy(pcp_lease_t *lease) {
  assert(lease);
  assert(lease->common.pending_sends == 0);
  pcp_lease_cancel_shutdown_timer(lease);
  lease_remove_timer(lease);
  packet_destroy(lease->common.request, lease->common.client->allocator);
  *lease = (pcp_lease_t){0};
}

static void lease_shutdown_complete(pcp_lease_t *lease) {
  lease->state = SHUTDOWN;
  pcp_lease_cancel_shutdown_timer(lease);
  auto const callback = lease->common.shutdown_callback;
  void *userdata = lease->common.shutdown_userdata;
  pcp_optional_error_t const result = lease->common.shutdown_error;
  lease->common.shutdown_callback = nullptr;
  lease->common.shutdown_userdata = nullptr;
  callback(userdata, result);
}

static void lease_send_delete(pcp_lease_t *lease) {
  assert(lease);
  assert(lease->state == SHUTTING_DOWN);
  assert(!lease->common.shutdown_delete_sent);
  assert(lease->common.pending_sends == 0);
  assert(lease->common.request);

  pcp_transport_view_t view = {.size = lease->common.request->size,
                               .data = lease->common.request->data};
  pcp_request_header_t header = pcp_packet_read_request_header(
      (pcp_transport_const_view_t){.size = view.size, .data = view.data});
  header.ttl = 0;
  pcp_packet_write_request_header(view, header);
  lease->common.shutdown_delete_sent = true;
  lease_send(lease);
}

void pcp_lease_shutdown(pcp_lease_t *lease,
                        time_t timeout_seconds,
                        void (*callback)(void *userdata,
                                         pcp_optional_error_t result),
                        void *userdata) {
  assert(lease);
  assert(timeout_seconds >= 0);
  assert(callback);
  assert(lease->state == PENDING || lease->state == ACTIVE ||
         lease->state == RENEWING || lease->state == FAILED);

  lease->common.shutdown_callback = callback;
  lease->common.shutdown_userdata = userdata;
  if (timeout_seconds > 0) {
    auto const timer =
        CALL(&lease->common.transport, add_timer,
             (pcp_transport_timeout_t){.seconds = (double)timeout_seconds},
             (pcp_transport_on_timer_t){.fn = lease_on_shutdown_timeout,
                                        .userdata = lease});
    if (timer.has_value) {
      lease->common.shutdown_timer = timer.value;
      lease->common.shutdown_response_timer_active = true;
    } else {
      lease->common.shutdown_error =
          (pcp_optional_error_t){.has_error = true, .error = timer.error};
    }
  }

  lease_remove_timer(lease);
  lease->state = SHUTTING_DOWN;
  if (lease->common.pending_sends == 0) {
    lease_send_delete(lease);
  }
}

static void lease_on_shutdown_timeout(void *userdata) {
  pcp_lease_t *lease = userdata;
  if (!lease->common.shutdown_response_timer_active) {
    return;
  }
  lease->common.shutdown_response_timer_active = false;

  if (!lease->common.shutdown_error.has_error) {
    lease->common.shutdown_error =
        (pcp_optional_error_t){.has_error = true, .error = ETIMEDOUT};
  }
  if (lease->common.shutdown_delete_sent && lease->common.pending_sends == 0) {
    lease_shutdown_complete(lease);
    return;
  }
  if (!lease->common.shutdown_delete_sent && lease->common.pending_sends == 0) {
    lease_send_delete(lease);
  }
}

bool pcp_lease_on_shutdown_response(pcp_lease_t *lease,
                                    pcp_response_t const *response) {
  if (!lease->common.shutdown_response_timer_active ||
      lease->state != SHUTTING_DOWN || !lease->common.shutdown_delete_sent ||
      !pcp_lease_response_matches(lease, response)) {
    return false;
  }

  pcp_lease_cancel_shutdown_timer(lease);
  lease->common.shutdown_response_received = true;
  if (lease->common.pending_sends == 0) {
    lease_shutdown_complete(lease);
  }
  return true;
}

void pcp_pending_state_create(pcp_lease_t *lease,
                              pcp_client_internal_t *client,
                              packet_t *request) {
  assert(lease);
  assert(client);
  assert(client->transport.send);
  assert(client->transport.add_timer);
  assert(client->transport.remove_timer);
  assert(request);
  lease->common = (pcp_lease_common_t){.transport = client->transport,
                                       .time = client->time,
                                       .client = client,
                                       .request = request};
  lease_enter_pending(lease,
                      (pcp_transport_timeout_t){
                          .seconds = (1.0 + rand_in_range(-0.1, 0.1)) * 3.0});
}

void pcp_lease_start(pcp_lease_t *lease,
                     pcp_client_internal_t *client,
                     packet_t *request) {
  assert(client);
  pcp_pending_state_create(lease, client, request);
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
