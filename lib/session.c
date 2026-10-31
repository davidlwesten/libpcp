#include "session.h"

#include "wire.h"

#include <assert.h>
#include <stdlib.h>
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

// free the mappings that are done
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

  // everything is up to date now, so released mappings can go
  pcp_session_free_released(session);

  // all mappings are up to date, so every deadline is in the future
  return (pcp_event_t){
      .type = PCP_EVENT_TIMEOUT,
      .timeout =
          {
              .deadline = pcp_session_deadline(session),
          },
  };
}

void pcp_session_end(pcp_session_t *session) {
  assert(session && session->config.allocator.free);

  for (auto iter = session->mappings; iter;) {
    auto const next = iter->next;
    FREE(session->config.allocator, iter);
    iter = next;
  }

  FREE(session->config.allocator, session);
}

// Suggest "any" address, but in the client's own family since the server picks
// one from the suggested family
static socket_address_t pcp_session_any_external(ip_address_t const *internal) {
  return (socket_address_t){
      .ip =
          ip_address_is_ipv4(internal) ? ip_address_any_v4 : ip_address_any_v6,
      .port = 0,
  };
}

static pcp_handle_or_error_t pcp_session_error(errno_t error) {
  return (pcp_handle_or_error_t){
      .has_value = false,
      .error = error,
  };
}

// an address as the wire format stores it
static void pcp_session_set_address(uint16_t *port,
                                    uint8_t ip[16],
                                    socket_address_t address) {
  *port = address.port;
  memcpy(ip, address.ip.addr, sizeof(address.ip.addr));
}

// a fresh nonce, which identifies the responses to a request
static bool pcp_session_nonce(pcp_session_t const *session,
                              size_t size,
                              uint8_t nonce[size]) {
  auto const rng = session->config.rng;
  return rng.fill(rng.ctx, size, nonce);
}

// the header of a request from this client, the caller fills in the body
static struct pcp_request pcp_session_new_request(pcp_session_t const *session,
                                                  enum pcp_opcode opcode,
                                                  pcp_time_s_t lifetime) {
  struct pcp_request result = {.opcode = opcode, .lifetime = lifetime};
  memcpy(result.client_ip, session->config.local.addr,
         sizeof(result.client_ip));
  return result;
}

// start a mapping for a MAP or PEER request
static pcp_handle_or_error_t
pcp_session_add(pcp_session_t *session,
                struct pcp_request const *request,
                size_t count,
                pcp_option_t const options[count]) {
  // has to fit in a single packet
  if (pcp_packet_request_size(request, count, options) > PCP_PACKET_MAX_SIZE) {
    return pcp_session_error(EMSGSIZE);
  }

  pcp_mapping_t *mapping =
      ALLOC(session->config.allocator,
            sizeof(pcp_mapping_t) + sizeof(pcp_option_t) * count);
  if (!mapping) {
    return pcp_session_error(ENOMEM);
  }

  auto const handle = ++session->handles;
  pcp_mapping_init(mapping, handle, *request, count, options);

  mapping->next = session->mappings;
  session->mappings = mapping;

  return (pcp_handle_or_error_t){
      .has_value = true,
      .value = handle,
  };
}

pcp_handle_or_error_t pcp_session_map_request(pcp_session_t *session,
                                              pcp_time_s_t lifetime,
                                              pcp_protocol_t protocol,
                                              port_t port,
                                              size_t count,
                                              pcp_option_t const options[]) {
  assert(session);
  // lifetime 0 means delete
  assert(lifetime > 0);

  auto request = pcp_session_new_request(session, PCP_OPCODE_MAP, lifetime);
  auto const body = &request.body.map_body;
  body->protocol = protocol;
  body->internal_port = port;
  pcp_session_set_address(&body->external_port, body->external_ip,
                          pcp_session_any_external(&session->config.local));

  if (!pcp_session_nonce(session, sizeof(body->nonce), body->nonce)) {
    return pcp_session_error(ENOTRECOVERABLE);
  }

  return pcp_session_add(session, &request, count, options);
}

pcp_handle_or_error_t pcp_session_peer_request(pcp_session_t *session,
                                               pcp_time_s_t lifetime,
                                               pcp_protocol_t protocol,
                                               port_t port,
                                               socket_address_t peer,
                                               size_t count,
                                               pcp_option_t const options[]) {
  assert(session);
  // lifetime 0 means delete
  assert(lifetime > 0);

  auto request = pcp_session_new_request(session, PCP_OPCODE_PEER, lifetime);
  auto const body = &request.body.peer_body;
  body->protocol = protocol;
  body->internal_port = port;
  pcp_session_set_address(&body->external_port, body->external_ip,
                          pcp_session_any_external(&session->config.local));
  pcp_session_set_address(&body->peer_port, body->peer_ip, peer);

  if (!pcp_session_nonce(session, sizeof(body->nonce), body->nonce)) {
    return pcp_session_error(ENOTRECOVERABLE);
  }

  return pcp_session_add(session, &request, count, options);
}

// look up a mapping the application still owns
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

  pcp_mapping_release(mapping);
}

void pcp_session_on_datagram(pcp_session_t *session,
                             pcp_time_ms_t now,
                             size_t size,
                             uint8_t const data[]) {
  assert(session);
  assert(data);

  auto const response =
      pcp_packet_read_response((const_view_t){.size = size, .data = data});
  if (!response.has_value) {
    return;
  }

  if (!pcp_epoch_update(&session->epoch, now, response.value.epoch)) {
    // The server lost its state, so request everything again. If this was due
    // to ANNOUNCE wait a random 0-5s first because every client on the network
    // got it at the same time
    pcp_time_ms_t not_before = 0;
    if (response.value.opcode == PCP_OPCODE_ANNOUNCE) {
      auto const rng = session->config.rng;
      uint16_t random = 0;
      if (rng.fill(rng.ctx, sizeof(random), (uint8_t *)&random)) {
        not_before = now + 5000 * (pcp_time_ms_t)random / UINT16_MAX;
      }
    }

    for (auto iter = session->mappings; iter; iter = iter->next) {
      pcp_mapping_reset(iter, not_before);
    }

    // don't trust the rest of the response, it could be stale
    return;
  }

  // ANNOUNCE only carries the epoch
  if (response.value.opcode == PCP_OPCODE_ANNOUNCE) {
    return;
  }

  for (auto iter = session->mappings; iter; iter = iter->next) {
    pcp_mapping_on_response(iter, &response.value, now, session->config.rng);
  }
}
