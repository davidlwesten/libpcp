#include <pcp/pcp.h>

#include "response.h"
#include "session.h"
#include "wire.h"

#include <assert.h>
#include <malloc.h>
#include <string.h>

#define ALLOC(allocator, ...) (allocator.alloc(allocator.ctx, __VA_ARGS__))
#define FREE(allocator, ...) (allocator.free(allocator.ctx, __VA_ARGS__))

static void *pcp_default_alloc(void * /*ctx*/, size_t size) {
  return malloc(size);
}

static void pcp_default_free(void * /*ctx*/, void *ptr) { free(ptr); }

pcp_session_t *pcp_session_begin(pcp_config_t config) {
  if (!config.allocator.alloc && !config.allocator.free) {
    config.allocator.alloc = pcp_default_alloc;
    config.allocator.free = pcp_default_free;
  }
  assert(config.allocator.alloc && config.allocator.free);
  assert(config.rng.random);
  assert(config.buffer.data && config.buffer.size >= PCP_PACKET_MAX_SIZE);

  auto const ctx = (pcp_session_t *)config.allocator.alloc(
      config.allocator.ctx, sizeof(pcp_session_t));
  if (ctx) {
    *ctx = (pcp_session_t){
        .config = config,
        .epoch = {0},
        .mappings = nullptr,
    };
  }

  return ctx;
}

static pcp_optional_time_ms_t
pcp_session_deadline(pcp_session_t const *session) {
  pcp_optional_time_ms_t earliest = {.has_value = false, .value = 0};
  for (auto iter = session->mappings; iter; iter = iter->next) {
    if (iter->state != PENDING) {
      continue;
    }

    auto const deadline = pcp_schedule_deadline(&iter->schedule);
    if (!earliest.has_value || deadline < earliest.value) {
      earliest = (pcp_optional_time_ms_t){.has_value = true, .value = deadline};
    }
  }

  return earliest;
}

pcp_event_t pcp_session_poll(pcp_session_t *session, pcp_time_ms_t now) {
  assert(session);

  pcp_event_t event = {0};
  for (auto iter = session->mappings; iter; iter = iter->next) {
    if (pcp_mapping_update(iter, now, &session->config, &event)) {
      return event;
    }
  }

  // every mapping is up to date with `now`, so all deadlines lie in the future
  return (pcp_event_t){
      .type = PCP_EVENT_TIMEOUT,
      .timeout = {.deadline = pcp_session_deadline(session)},
  };
}

void pcp_session_end(pcp_session_t *ctx) {
  assert(ctx && ctx->config.allocator.free);

  for (auto iter = ctx->mappings; iter;) {
    auto const next = iter->next;
    FREE(ctx->config.allocator, iter);
    iter = next;
  }

  FREE(ctx->config.allocator, ctx);
}

pcp_handle_or_error_t
pcp_session_map_request(pcp_session_t *session,
                        pcp_time_s_t lifetime,
                        pcp_protocol_t protocol,
                        port_t port,
                        size_t count,
                        pcp_option_t const options[count]) {
  assert(session);

  pcp_mapping_t *mapping =
      ALLOC(session->config.allocator,
            sizeof(pcp_mapping_t) + sizeof(pcp_option_t) * count);
  if (!mapping) {
    return (pcp_handle_or_error_t){
        .has_value = false,
        .error = ENOMEM,
    };
  }

  pcp_nonce_t nonce = {0};
  if (!pcp_nonce_generate(session->config.rng, &nonce)) {
    FREE(session->config.allocator, mapping);
    return (pcp_handle_or_error_t){
        .has_value = false,
        .error = ENOTRECOVERABLE,
    };
  }

  *mapping = (pcp_mapping_t){
      .next = nullptr,
      .request =
          {
              .lifetime = lifetime,
              .protocol = protocol,
              .port = port,
              .type = MAP,
              .map =
                  {
                      .external = {0},
                  },
          },
      .nonce = nonce,
      .state = PENDING,
      .count = count,
  };

  memcpy(mapping->options, options, sizeof(pcp_option_t) * count);

  pcp_schedule_start(&mapping->schedule, PCP_BACKOFF_DEFAULT, 0);

  return (pcp_handle_or_error_t){
      .has_value = true,
      .value = pcp_session_add_mapping(session, mapping),
  };
}

pcp_handle_or_error_t
pcp_session_peer_request(pcp_session_t *session,
                         pcp_time_s_t lifetime,
                         pcp_protocol_t protocol,
                         port_t port,
                         socket_address_t peer,
                         size_t count,
                         const pcp_option_t options[count]) {
  assert(session);

  pcp_mapping_t *mapping =
      ALLOC(session->config.allocator,
            sizeof(pcp_mapping_t) + sizeof(pcp_option_t) * count);

  if (!mapping) {
    return (pcp_handle_or_error_t){
        .has_value = false,
        .error = ENOMEM,
    };
  }

  pcp_nonce_t nonce = {0};
  if (!pcp_nonce_generate(session->config.rng, &nonce)) {
    FREE(session->config.allocator, mapping);
    return (pcp_handle_or_error_t){
        .has_value = false,
        .error = ENOTRECOVERABLE,
    };
  }

  *mapping = (pcp_mapping_t){
      .next = nullptr,
      .request =
          {
              .lifetime = lifetime,
              .protocol = protocol,
              .port = port,
              .type = PEER,
              .peer =
                  {
                      .external = {0},
                      .peer = peer,
                  },
          },
      .nonce = nonce,
      .state = PENDING,
      .count = count,
  };

  memcpy(mapping->options, options, sizeof(pcp_option_t) * count);

  pcp_schedule_start(&mapping->schedule, PCP_BACKOFF_DEFAULT, 0);

  return (pcp_handle_or_error_t){
      .has_value = true,
      .value = pcp_session_add_mapping(session, mapping),
  };
}

void pcp_session_on_datagram(pcp_session_t *session,
                             pcp_time_ms_t now,
                             size_t size,
                             uint8_t const data[size]) {
  assert(session);
  assert(data);

  pcp_response_t response = {0};
  if (!pcp_packet_read_response((const_view_t){.size = size, .data = data},
                                &response)) {
    return;
  }

  if (!pcp_epoch_update(&session->epoch, now, response.epoch)) {
    for (auto iter = session->mappings; iter; iter = iter->next) {
      pcp_mapping_on_server_reset(iter);
    }
  }

  // todo: match the response to its mapping
}
