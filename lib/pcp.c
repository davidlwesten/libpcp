#include <asm-generic/errno.h>
#include <inttypes.h>
#include <pcp/pcp.h>

#include "request.h"
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

  auto const ctx = (pcp_session_t *)config.allocator.alloc(
      config.allocator.ctx, sizeof(pcp_session_t));
  if (ctx) {
    *ctx = (pcp_session_t){
        .config = config,
        .mappings = nullptr,
    };
  }

  return ctx;
}

bool pcp_session_pop_event(pcp_session_t *session,
                           time_t now,
                           pcp_event_t *out) {
  assert(session);
  assert(out);

  for (auto iter = session->mappings; iter; iter = iter->next) {
    if (pcp_mapping_update(iter, now, out)) {
      return true;
    }
  }

  return false;
}

bool pcp_session_pop_packet(pcp_session_t *session, pcp_datagram_t *out) {
  assert(session);
  assert(out);

  for (auto iter = session->mappings; iter; iter = iter->next) {
    if (iter->state != PENDING) {
      continue;
    }
    // todo: this is wrong! Just doing this for testing
    iter->state = ACTIVE;

    out->id = (pcp_mapping_t)iter;
    out->size = pcp_packet_write_request(
        (view_t){.size = out->size, .data = out->data}, &iter->request,
        &session->config.local, iter->count, iter->options);

    return true;
  }

  return false;
}

void pcp_session_end(pcp_session_t *ctx) {
  assert(ctx && ctx->config.allocator.free);
  FREE(ctx->config.allocator, ctx);
}

pcp_mapping_or_error_t
pcp_session_map_request(pcp_session_t *session,
                        time_t lifetime,
                        pcp_protocol_t protocol,
                        port_t port,
                        size_t count,
                        pcp_option_t const options[count]) {
  assert(session);

  pcp_mapping_t *mapping =
      ALLOC(session->config.allocator,
            sizeof(pcp_mapping_t) + sizeof(pcp_option_t) * count);
  if (!mapping) {
    return (pcp_mapping_or_error_t){
        .has_value = false,
        .error = ENOMEM,
    };
  }

  pcp_nonce_t nonce = {0};
  if (!pcp_session_generate_nonce(session, &nonce)) {
    return (pcp_mapping_or_error_t){
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

  return (pcp_handle_or_error_t){
      .has_value = true,
      .value = pcp_session_add_mapping(session, mapping),
  };
}

pcp_mapping_or_error_t
pcp_session_peer_request(pcp_session_t *session,
                         time_t lifetime,
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
    return (pcp_mapping_or_error_t){
        .has_value = false,
        .error = ENOMEM,
    };
  }

  pcp_nonce_t nonce = {0};
  if (!pcp_session_generate_nonce(session, &nonce)) {
    return (pcp_mapping_or_error_t){
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

  return (pcp_handle_or_error_t){
      .has_value = true,
      .value = pcp_session_add_mapping(session, mapping),
  };
}
