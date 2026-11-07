#include "server.h"

#include <string.h>
#include <unity.h>

#include "pcp_gen.h"

static constexpr uint8_t VERSION = 2;
static constexpr uint32_t ERROR_LIFETIME = 30;
static constexpr port_t FIRST_EXTERNAL_PORT = 40000;

static_assert(SIM_SERVER_RESPONSE_SIZE ==
              PCP_RESPONSE_HEADER_MIN_SIZE + PCP_MAP_RESPONSE_MIN_SIZE);

// what a generated decoder can't know is wrong, it already checks the rest of
// the format
static void check_request(ip_address_t const *source,
                          struct pcp_request const *request) {
  TEST_ASSERT_EQUAL_MESSAGE(PCP_OPCODE_MAP, request->opcode,
                            "only MAP is simulated");
  TEST_ASSERT_MESSAGE(
      memcmp(request->client_ip, source->addr, sizeof(source->addr)) == 0,
      "client address doesn't match the source address");
  TEST_ASSERT_MESSAGE(request->body.map_body.protocol != ALL &&
                          request->body.map_body.internal_port != 0,
                      "mappings of all protocols or ports aren't simulated");
}

static void expire_leases(sim_server_t *server, pcp_time_ms_t now) {
  for (size_t i = 0; i < SIM_SERVER_MAX_LEASES; ++i) {
    auto const lease = &server->leases[i];
    if (lease->used && lease->expires <= now) {
      lease->used = false;
      ++server->stats.expired;
    }
  }
}

static sim_lease_t *
find_lease(sim_server_t *server, uint8_t protocol, port_t internal_port) {
  for (size_t i = 0; i < SIM_SERVER_MAX_LEASES; ++i) {
    auto const lease = &server->leases[i];
    if (lease->used && lease->protocol == protocol &&
        lease->internal_port == internal_port) {
      return lease;
    }
  }
  return nullptr;
}

static bool port_is_free(sim_server_t const *server, port_t port) {
  if (port == 0) {
    return false;
  }

  for (size_t i = 0; i < SIM_SERVER_MAX_LEASES; ++i) {
    auto const lease = &server->leases[i];
    if (lease->used && lease->external.port == port) {
      return false;
    }
  }
  return true;
}

// takes the suggested port if it can, the suggested address never matters since
// the server only has the one
static port_t allocate_port(sim_server_t *server, port_t suggested) {
  if (port_is_free(server, suggested)) {
    return suggested;
  }

  while (true) {
    auto const port = (port_t)(FIRST_EXTERNAL_PORT + server->ports_allocated++);
    if (port_is_free(server, port)) {
      return port;
    }
  }
}

static sim_lease_t *create_lease(sim_server_t *server,
                                 struct pcp_map_body const *map) {
  for (size_t i = 0; i < SIM_SERVER_MAX_LEASES; ++i) {
    auto const lease = &server->leases[i];
    if (lease->used) {
      continue;
    }

    *lease = (sim_lease_t){
        .used = true,
        .protocol = (pcp_protocol_t)map->protocol,
        .internal_port = map->internal_port,
        .external =
            {
                .ip = server->external_ip,
                .port = allocate_port(server, map->external_port),
            },
    };
    memcpy(lease->nonce, map->nonce, sizeof(lease->nonce));
    return lease;
  }

  TEST_FAIL_MESSAGE("the simulated server ran out of leases");
  return nullptr;
}

static void set_external(struct pcp_map_response *body,
                         socket_address_t const *external) {
  body->external_port = external->port;
  memcpy(body->external_ip, external->ip.addr, sizeof(body->external_ip));
}

// updates the leases for `request`, and `body` with what the response says
static pcp_result_t apply_request(sim_server_t *server,
                                  pcp_time_ms_t now,
                                  struct pcp_request const *request,
                                  sim_server_decision_t const *decision,
                                  struct pcp_map_response *body) {
  if (decision->refuse != SUCCESS) {
    ++server->stats.refused;
    body->lifetime = decision->error_lifetime;
    return decision->refuse;
  }

  auto const map = &request->body.map_body;
  auto lease = find_lease(server, map->protocol, map->internal_port);

  // only the nonce that created a mapping can change it, section 11.3
  if (lease && memcmp(lease->nonce, map->nonce, sizeof(lease->nonce)) != 0) {
    body->lifetime = ERROR_LIFETIME;
    return NOT_AUTHORIZED;
  }

  // a delete succeeds whether or not there's anything to delete, section 15.1
  if (request->lifetime == 0) {
    if (lease) {
      set_external(body, &lease->external);
      lease->used = false;
      ++server->stats.deleted;
    }
    return SUCCESS;
  }

  if (lease) {
    ++server->stats.renewed;
  } else {
    lease = create_lease(server, map);
    ++server->stats.created;
  }

  // a server can grant less than requested, section 15
  auto const max = decision->max_lifetime;
  body->lifetime =
      max != 0 && request->lifetime > max ? max : request->lifetime;

  lease->expires = now + (pcp_time_ms_t)body->lifetime * 1000;
  set_external(body, &lease->external);
  return SUCCESS;
}

static uint32_t epoch_of(sim_server_t const *server, pcp_time_ms_t now) {
  return (uint32_t)((now - server->started) / 1000);
}

// the encoders take the header and what follows it separately, see pcp.pkt
static size_t write_header(uint8_t version,
                           enum pcp_opcode opcode,
                           pcp_result_t result,
                           size_t rest_size,
                           uint8_t const rest[rest_size],
                           uint8_t data[SIM_SERVER_RESPONSE_SIZE]) {
  struct pcp_response_header const header = {
      .version = version,
      .r = 1,
      .opcode = opcode,
      .result = result,
      .rest = {.ptr = rest, .len = rest_size},
  };
  size_t size = 0;
  TEST_ASSERT_EQUAL(PCP_OK, pcp_response_header_encode(&header, data,
                                                       SIM_SERVER_RESPONSE_SIZE,
                                                       &size));
  return size;
}

static size_t write_response(pcp_result_t result,
                             struct pcp_map_response const *body,
                             uint8_t data[SIM_SERVER_RESPONSE_SIZE]) {
  uint8_t rest[PCP_MAP_RESPONSE_MIN_SIZE];
  size_t rest_size = 0;
  TEST_ASSERT_EQUAL(
      PCP_OK, pcp_map_response_encode(body, rest, sizeof(rest), &rest_size));
  return write_header(VERSION, PCP_OPCODE_MAP, result, rest_size, rest, data);
}

// What a NAT-PMP router answers anything but NAT-PMP with (RFC 6886
// section 3.5): version 0 and result 1, without a response bit, followed by its
// epoch
static size_t write_natpmp_refusal(sim_server_t const *server,
                                   pcp_time_ms_t now,
                                   uint8_t data[SIM_SERVER_RESPONSE_SIZE]) {
  auto const epoch = epoch_of(server, now);
  uint8_t const rest[] = {(uint8_t)(epoch >> 24), (uint8_t)(epoch >> 16),
                          (uint8_t)(epoch >> 8), (uint8_t)epoch};
  struct pcp_response_header const header = {
      .version = 0,
      .r = 0,
      .opcode = PCP_OPCODE_ANNOUNCE,
      .result = UNSUPP_VERSION,
      .rest = {.ptr = rest, .len = sizeof(rest)},
  };
  size_t size = 0;
  TEST_ASSERT_EQUAL(PCP_OK, pcp_response_header_encode(&header, data,
                                                       SIM_SERVER_RESPONSE_SIZE,
                                                       &size));
  return size;
}

sim_server_decision_t sim_server_always(void *ctx,
                                        pcp_time_s_t /*lifetime*/,
                                        pcp_protocol_t /*protocol*/,
                                        port_t /*internal_port*/) {
  sim_server_decision_t const *decision = ctx;
  return *decision;
}

void sim_server_start(sim_server_t *server,
                      ip_address_t external_ip,
                      pcp_time_ms_t now) {
  memset(server->leases, 0, sizeof(server->leases));
  server->external_ip = external_ip;
  server->started = now;
}

void sim_server_set_behavior(sim_server_t *server,
                             sim_server_behavior_t behavior) {
  server->behavior = behavior;
}

sim_server_reply_t sim_server_on_datagram(sim_server_t *server,
                                          pcp_time_ms_t now,
                                          ip_address_t source,
                                          size_t size,
                                          uint8_t const data[size]) {
  struct pcp_request request;
  TEST_ASSERT_EQUAL_MESSAGE(PCP_OK, pcp_request_decode(&request, data, size),
                            "malformed request, or one with options, which "
                            "aren't simulated yet");
  check_request(&source, &request);

  expire_leases(server, now);
  ++server->stats.requests;

  sim_server_decision_t decision = {0};
  if (server->behavior.on_request) {
    auto const map = &request.body.map_body;
    decision = server->behavior.on_request(
        server->behavior.ctx, request.lifetime, (pcp_protocol_t)map->protocol,
        map->internal_port);
  }

  if (decision.natpmp) {
    sim_server_reply_t reply = {
        .result = UNSUPP_VERSION,
    };
    reply.size = write_natpmp_refusal(server, now, reply.data);
    return reply;
  }

  // the client matches responses on what's echoed here, section 11.4
  auto const map = &request.body.map_body;
  struct pcp_map_response body = {
      .lifetime = request.lifetime,
      .epoch = epoch_of(server, now),
      .protocol = map->protocol,
      .internal_port = map->internal_port,
      .external_port = map->external_port,
  };
  memcpy(body.nonce, map->nonce, sizeof(body.nonce));
  memcpy(body.external_ip, map->external_ip, sizeof(body.external_ip));

  sim_server_reply_t reply = {
      .result = apply_request(server, now, &request, &decision, &body),
  };
  reply.size = write_response(reply.result, &body, reply.data);
  return reply;
}

sim_lease_t const *sim_server_find_lease(sim_server_t *server,
                                         pcp_time_ms_t now,
                                         pcp_protocol_t protocol,
                                         port_t internal_port) {
  expire_leases(server, now);
  return find_lease(server, protocol, internal_port);
}
