#pragma once

#include <pcp/pcp.h>

// The network between the client and the server. It carries opaque datagrams
// and knows nothing about what's in them, what happens to each of them is up to
// a behavior.

/**
 * @brief A length of time in milliseconds, pcp_time_ms_t being a point in time
 */
typedef pcp_time_ms_t sim_duration_ms_t;

static constexpr size_t SIM_NETWORK_MAX_IN_FLIGHT = 64;
static constexpr size_t SIM_NETWORK_MAX_DATAGRAM_SIZE = 1500;

typedef enum sim_endpoint {
  SIM_ENDPOINT_CLIENT,
  SIM_ENDPOINT_SERVER,
} sim_endpoint_t;

typedef struct sim_datagram {
  pcp_time_ms_t deliver_at;
  sim_endpoint_t to;
  size_t size;
  uint8_t data[SIM_NETWORK_MAX_DATAGRAM_SIZE];
} sim_datagram_t;

/**
 * @brief What happens to a datagram, all zeroes means it arrives normally
 */
typedef struct sim_fate {
  bool lost;
  /// On top of the network's latency, which can also reorder datagrams
  sim_duration_ms_t delay;
  /// Whether a second copy arrives too, after its own delay
  bool duplicated;
  sim_duration_ms_t duplicate_delay;
} sim_fate_t;

/**
 * @brief Decides what happens to each datagram
 *
 * `on_send` can look at the datagram but shouldn't change anything outside of
 * its `ctx`, since it runs in the middle of a send. Any randomness it needs
 * belongs in its `ctx`, started from the scenario's seed (see sim_random) so
 * that a seed keeps giving the same run.
 *
 * Without an `on_send` every datagram arrives normally. `ctx` has to outlive
 * the behavior's use.
 */
typedef struct sim_network_behavior {
  sim_fate_t (*on_send)(void *ctx,
                        sim_endpoint_t to,
                        size_t size,
                        uint8_t const data[]);
  void *ctx;
} sim_network_behavior_t;

/**
 * @brief The ctx of sim_random_faults, applying to each datagram independently
 */
typedef struct sim_random_faults_config {
  uint8_t loss_percent;
  uint8_t duplicate_percent;
  /// Up to this much on top of the latency, separately for duplicates
  sim_duration_ms_t jitter;
  /// The state for sim_random
  uint64_t rng;
} sim_random_faults_config_t;

sim_fate_t sim_random_faults(void *ctx,
                             sim_endpoint_t to,
                             size_t size,
                             uint8_t const data[]);

/**
 * @brief A behavior with these sim_random_faults_config_t fields
 *
 * The seed is its own argument so it can't be forgotten, which would give every
 * seed the same faults. It's mixed with a constant so that faults don't draw
 * the same numbers as the client.
 */
#define SIM_RANDOM_FAULTS(seed, ...)                                           \
  (sim_network_behavior_t) {                                                   \
    .on_send = sim_random_faults, .ctx = &(sim_random_faults_config_t) {       \
      .rng = (uint64_t)(seed) ^ 0x6E6574776F726B00, __VA_ARGS__                \
    }                                                                          \
  }

sim_fate_t sim_lose_everything(void *ctx,
                               sim_endpoint_t to,
                               size_t size,
                               uint8_t const data[]);

/**
 * @brief Everything sent is lost, what's already in flight still arrives
 */
#define SIM_NETWORK_DOWN                                                       \
  (sim_network_behavior_t) { .on_send = sim_lose_everything }

/**
 * @brief Everything sent arrives normally
 */
#define SIM_NETWORK_UP (sim_network_behavior_t){0}

typedef struct sim_network_stats {
  size_t lost;
  size_t duplicated;
} sim_network_stats_t;

typedef struct sim_network {
  /// Each way
  sim_duration_ms_t latency;
  sim_network_behavior_t behavior;
  sim_network_stats_t stats;
  size_t in_flight;
  sim_datagram_t datagrams[SIM_NETWORK_MAX_IN_FLIGHT];
} sim_network_t;

/**
 * @brief Get the next number from `state`, a sequence that only depends on the
 *        state it started with and is the same on every platform
 */
uint64_t sim_random(uint64_t *state);

/**
 * @brief Start a network where everything arrives normally, `network` has to
 *        start out zeroed
 */
void sim_network_start(sim_network_t *network, sim_duration_ms_t latency);

void sim_network_set_behavior(sim_network_t *network,
                              sim_network_behavior_t behavior);

/**
 * @returns Whether the datagram is on its way, instead of lost
 */
bool sim_network_send(sim_network_t *network,
                      pcp_time_ms_t now,
                      sim_endpoint_t to,
                      size_t size,
                      uint8_t const data[size]);

/**
 * @brief Take the next datagram that has arrived by `now`
 *
 * @returns Whether there was one
 */
bool sim_network_receive(sim_network_t *network,
                         pcp_time_ms_t now,
                         sim_datagram_t *out);

/**
 * @brief Get when the next datagram arrives
 *
 * @returns Whether anything is in flight
 */
bool sim_network_next_arrival(sim_network_t const *network, pcp_time_ms_t *out);

size_t sim_network_in_flight(sim_network_t const *network);
