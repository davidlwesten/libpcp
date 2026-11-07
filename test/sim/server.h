#pragma once

#include <pcp/pcp.h>

// A PCP server (RFC 6887) for the simulation. It tracks real leases and only
// knows the client through the bytes it sends. It shares the library's
// generated wire format (pcp_gen.h), test/unit/wire.c is what pins that to the
// RFC's layout.
//
// A request no real server should have to accept fails the test instead of
// getting an error response. Refusing a well-formed request is regular server
// behavior, and gets a response.
//
// For now it serves a single client's MAP requests without options.

static constexpr size_t SIM_SERVER_MAX_LEASES = 16;
static constexpr size_t SIM_SERVER_RESPONSE_SIZE = 60;

/**
 * @brief What the server does with a request, all zeroes means it handles it
 *        normally
 */
typedef struct sim_server_decision {
  /// Answer with this error instead, if it's not SUCCESS
  pcp_result_t refuse;
  /// How long the error lasts, according to its response
  pcp_time_s_t error_lifetime;
  /// Grant at most this lifetime, 0 for whatever is requested
  pcp_time_s_t max_lifetime;
  /// Answer like a router that only speaks NAT-PMP (RFC 6887 appendix A)
  bool natpmp;
} sim_server_decision_t;

/**
 * @brief Decides what the server does with each well-formed request
 *
 * `on_request` shouldn't change anything outside of its `ctx`, since it runs in
 * the middle of handling a request. Any randomness it needs belongs in its
 * `ctx`, started from the scenario's seed. A lifetime of 0 is a delete.
 *
 * Without an `on_request` every request is handled normally. `ctx` has to
 * outlive the behavior's use.
 */
typedef struct sim_server_behavior {
  sim_server_decision_t (*on_request)(void *ctx,
                                      pcp_time_s_t lifetime,
                                      pcp_protocol_t protocol,
                                      port_t internal_port);
  void *ctx;
} sim_server_behavior_t;

/**
 * @brief A behavior that decides the same for every request, its `ctx` being a
 *        sim_server_decision_t. See SIM_SERVER_ALWAYS.
 */
sim_server_decision_t sim_server_always(void *ctx,
                                        pcp_time_s_t lifetime,
                                        pcp_protocol_t protocol,
                                        port_t internal_port);

/**
 * @brief A behavior deciding these sim_server_decision_t fields for every
 *        request
 */
#define SIM_SERVER_ALWAYS(...)                                                 \
  (sim_server_behavior_t) {                                                    \
    .on_request = sim_server_always, .ctx = &(sim_server_decision_t) {         \
      __VA_ARGS__                                                              \
    }                                                                          \
  }

/**
 * @brief Every request is handled normally
 */
#define SIM_SERVER_NORMAL (sim_server_behavior_t){0}

/**
 * @brief Every request gets this error, see SIM_SERVER_ALWAYS
 */
#define SIM_SERVER_REFUSES(result, lifetime)                                   \
  SIM_SERVER_ALWAYS(.refuse = (result), .error_lifetime = (lifetime))

/**
 * @brief Every request is granted at most this lifetime, see SIM_SERVER_ALWAYS
 */
#define SIM_SERVER_GRANTS_AT_MOST(lifetime)                                    \
  SIM_SERVER_ALWAYS(.max_lifetime = (lifetime))

/**
 * @brief Every request is answered like a NAT-PMP-only router would
 */
#define SIM_SERVER_NATPMP_ONLY SIM_SERVER_ALWAYS(.natpmp = true)

typedef struct sim_lease {
  bool used;
  uint8_t nonce[12];
  pcp_protocol_t protocol;
  port_t internal_port;
  socket_address_t external;
  pcp_time_ms_t expires;
} sim_lease_t;

typedef struct sim_server_stats {
  size_t requests;
  size_t created;
  size_t renewed;
  size_t deleted;
  /// Leases that ran out instead of being renewed or deleted
  size_t expired;
  size_t refused;
} sim_server_stats_t;

typedef struct sim_server {
  sim_server_behavior_t behavior;
  ip_address_t external_ip;
  uint16_t ports_allocated;
  pcp_time_ms_t started;
  sim_lease_t leases[SIM_SERVER_MAX_LEASES];
  sim_server_stats_t stats;
} sim_server_t;

/**
 * @brief Boot the server, or reboot it, losing all leases and starting the
 * epoch over with this external address
 *
 * `server` has to start out zeroed. Its behavior and stats survive a reboot.
 */
void sim_server_start(sim_server_t *server,
                      ip_address_t external_ip,
                      pcp_time_ms_t now);

void sim_server_set_behavior(sim_server_t *server,
                             sim_server_behavior_t behavior);

/**
 * @brief Write the ANNOUNCE a server sends after it restarted (section 14.1.3)
 *
 * @returns The size of what was written
 */
size_t sim_server_announce(sim_server_t const *server,
                           pcp_time_ms_t now,
                           uint8_t data[SIM_SERVER_RESPONSE_SIZE]);

/**
 * @brief Write what looks like a successful response for this mapping, but with
 *        a nonce nobody sent, which is what an attacker without access to the
 *        requests could do. The epoch is the real one.
 *
 * @returns The size of what was written
 */
size_t sim_server_forge_response(sim_server_t const *server,
                                 pcp_time_ms_t now,
                                 pcp_protocol_t protocol,
                                 port_t internal_port,
                                 uint8_t data[SIM_SERVER_RESPONSE_SIZE]);

typedef struct sim_server_reply {
  /// What came of the request
  pcp_result_t result;
  /// The response to send back
  size_t size;
  uint8_t data[SIM_SERVER_RESPONSE_SIZE];
} sim_server_reply_t;

sim_server_reply_t sim_server_on_datagram(sim_server_t *server,
                                          pcp_time_ms_t now,
                                          ip_address_t source,
                                          size_t size,
                                          uint8_t const data[size]);

/**
 * @returns The lease that's live at `now`, or nullptr if there is none
 */
sim_lease_t const *sim_server_find_lease(sim_server_t *server,
                                         pcp_time_ms_t now,
                                         pcp_protocol_t protocol,
                                         port_t internal_port);
