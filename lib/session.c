#include "session.h"

#include "response.h"
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
  assert(config.rng.fill);
  assert(config.buffer.data && config.buffer.size >= PCP_PACKET_MAX_SIZE);

  pcp_session_t *ctx = ALLOC(config.allocator, sizeof(pcp_session_t));
  if (ctx) {
    *ctx = (pcp_session_t){
        .config = config,
        .epoch = {0},
        .mappings = nullptr,
        .handles = 0,
    };
  }

  return ctx;
}

static pcp_optional_time_ms_t
pcp_session_deadline(pcp_session_t const *session) {
  pcp_optional_time_ms_t earliest = {.has_value = false, .value = 0};
  for (auto iter = session->mappings; iter; iter = iter->next) {
    auto const deadline = pcp_mapping_deadline(iter);
    if (deadline.has_value &&
        (!earliest.has_value || deadline.value < earliest.value)) {
      earliest = deadline;
    }
  }

  return earliest;
}

// frees the mappings the application released that have nothing left to do
static void pcp_session_free_released(pcp_session_t *session) {
  auto link = &session->mappings;
  while (*link) {
    auto const mapping = *link;
    if (mapping->state.type == RELEASED) {
      *link = mapping->next;
      FREE(session->config.allocator, mapping);
    } else {
      link = &mapping->next;
    }
  }
}

pcp_event_t pcp_session_poll(pcp_session_t *session, pcp_time_ms_t now) {
  assert(session);

  pcp_event_t event = {0};
  for (auto iter = session->mappings; iter; iter = iter->next) {
    if (pcp_mapping_update(iter, now, &session->config, &event)) {
      return event;
    }
  }

  // released mappings are done for good once every mapping is up to date
  pcp_session_free_released(session);

  // every mapping is up to date with `now`, so all deadlines lie in the future
  return (pcp_event_t){
      .type = PCP_EVENT_TIMEOUT,
      .timeout =
          {
              .deadline = pcp_session_deadline(session),
          },
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

static void pcp_session_add_mapping(pcp_session_t *session,
                                    pcp_mapping_t *mapping) {
  assert(session);
  assert(mapping);
  assert(mapping->next == nullptr);

  if (!session->mappings) {
    session->mappings = mapping;
  } else {
    auto iter = session->mappings;
    while (iter->next) {
      iter = iter->next;
    }

    iter->next = mapping;
  }
}

static pcp_handle_or_error_t
pcp_session_request(pcp_session_t *session,
                    pcp_request_t request,
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

  // starting from 1, so a zero-initialised handle never refers to a mapping
  auto const handle = ++session->handles;
  pcp_mapping_init(mapping, handle, request, nonce, count, options);
  pcp_session_add_mapping(session, mapping);

  return (pcp_handle_or_error_t){
      .has_value = true,
      .value = handle,
  };
}

// no preference for the external address, other than it being in the client's
// own address family since the server assigns one of the suggested family
static socket_address_t pcp_session_any_external(pcp_session_t const *session) {
  return (socket_address_t){
      .ip = ip_address_is_ipv4_mapped(&session->config.local)
                ? ip_address_any_v4
                : ip_address_any_v6,
      .port = 0,
  };
}

pcp_handle_or_error_t
pcp_session_map_request(pcp_session_t *session,
                        pcp_time_s_t lifetime,
                        pcp_protocol_t protocol,
                        port_t port,
                        size_t count,
                        pcp_option_t const options[count]) {
  assert(session);
  return pcp_session_request(
      session,
      (pcp_request_t){
          .lifetime = lifetime,
          .protocol = protocol,
          .port = port,
          .type = MAP,
          .map =
              {
                  .external = pcp_session_any_external(session),
              },
      },
      count, options);
}

pcp_handle_or_error_t
pcp_session_peer_request(pcp_session_t *session,
                         pcp_time_s_t lifetime,
                         pcp_protocol_t protocol,
                         port_t port,
                         socket_address_t peer,
                         size_t count,
                         pcp_option_t const options[count]) {
  assert(session);
  return pcp_session_request(
      session,
      (pcp_request_t){
          .lifetime = lifetime,
          .protocol = protocol,
          .port = port,
          .type = PEER,
          .peer =
              {
                  .external = pcp_session_any_external(session),
                  .peer = peer,
              },
      },
      count, options);
}

// the mapping `handle` refers to, unless the application already released it
static pcp_mapping_t *pcp_session_find(pcp_session_t const *session,
                                       pcp_handle_t handle) {
  for (auto iter = session->mappings; iter; iter = iter->next) {
    if (iter->handle != handle) {
      continue;
    }

    switch (iter->state.type) {
    case RELEASING:
    case RELEASED:
      return nullptr;
    default:
      return iter;
    }
  }

  return nullptr;
}

void pcp_session_release(pcp_session_t *session, pcp_handle_t handle) {
  assert(session);

  auto const mapping = pcp_session_find(session, handle);
  if (!mapping) {
    return;
  }

  // freed by pcp_session_poll once it's RELEASED, which may take a few requests
  pcp_mapping_release(mapping);
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
    // RFC 6887 §8.5 asks to request every mapping again straight away, but an
    // ANNOUNCE reaches many clients at once so §14.1.3 asks to wait a random
    // time of up to 5s first (or not at all if the RNG fails)
    pcp_time_ms_t not_before = 0;
    if (response.type == ANNOUNCE) {
      auto const rng = session->config.rng;
      uint16_t random = 0;
      if (rng.fill(rng.ctx, sizeof(random), (uint8_t *)&random)) {
        not_before = now + 5'000 * (pcp_time_ms_t)random / UINT16_MAX;
      }
    }

    for (auto iter = session->mappings; iter; iter = iter->next) {
      pcp_mapping_reset(iter, not_before);
    }

    // the response may just as well be a stale one from before a restart, and
    // every mapping is requested again anyway, so don't act on it
    return;
  }

  // an ANNOUNCE only tells us about the epoch
  if (response.type == ANNOUNCE) {
    return;
  }

  for (auto iter = session->mappings; iter; iter = iter->next) {
    pcp_mapping_on_response(iter, &response, now, session->config.rng);
  }
}
