#include "network.h"

#include <string.h>
#include <unity.h>

// the next datagram to arrive, the one sent first if it's a tie
static size_t next_datagram(sim_network_t const *network) {
  size_t next = 0;
  for (size_t i = 1; i < network->in_flight; ++i) {
    if (network->datagrams[i].deliver_at <
        network->datagrams[next].deliver_at) {
      next = i;
    }
  }
  return next;
}

static void enqueue(sim_network_t *network,
                    pcp_time_ms_t now,
                    sim_duration_ms_t delay,
                    sim_endpoint_t to,
                    size_t size,
                    uint8_t const data[size]) {
  TEST_ASSERT_LESS_THAN_size_t_MESSAGE(SIM_NETWORK_MAX_IN_FLIGHT,
                                       network->in_flight,
                                       "too many datagrams in flight");

  auto const datagram = &network->datagrams[network->in_flight++];
  datagram->deliver_at = now + network->latency + delay;
  datagram->to = to;
  datagram->size = size;
  memcpy(datagram->data, data, size);
}

static bool roll(uint64_t *rng, uint8_t percent) {
  return sim_random(rng) % 100 < percent;
}

// splitmix64, small and independent of the platform's rand()
uint64_t sim_random(uint64_t *state) {
  *state += 0x9E3779B97F4A7C15;
  uint64_t value = *state;
  value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9;
  value = (value ^ (value >> 27)) * 0x94D049BB133111EB;
  return value ^ (value >> 31);
}

sim_fate_t sim_random_faults(void *ctx,
                             sim_endpoint_t /*to*/,
                             size_t /*size*/,
                             uint8_t const /*data*/[]) {
  sim_random_faults_config_t *config = ctx;
  return (sim_fate_t){
      .lost = roll(&config->rng, config->loss_percent),
      .delay = sim_random(&config->rng) % (config->jitter + 1),
      .duplicated = roll(&config->rng, config->duplicate_percent),
      .duplicate_delay = sim_random(&config->rng) % (config->jitter + 1),
  };
}

sim_fate_t sim_lose_everything(void * /*ctx*/,
                               sim_endpoint_t /*to*/,
                               size_t /*size*/,
                               uint8_t const /*data*/[]) {
  return (sim_fate_t){.lost = true};
}

void sim_network_start(sim_network_t *network, sim_duration_ms_t latency) {
  network->latency = latency;
}

void sim_network_set_behavior(sim_network_t *network,
                              sim_network_behavior_t behavior) {
  network->behavior = behavior;
}

bool sim_network_send(sim_network_t *network,
                      pcp_time_ms_t now,
                      sim_endpoint_t to,
                      size_t size,
                      uint8_t const data[size]) {
  TEST_ASSERT_LESS_OR_EQUAL_size_t_MESSAGE(
      SIM_NETWORK_MAX_DATAGRAM_SIZE, size,
      "datagram too large for the network");

  sim_fate_t fate = {0};
  if (network->behavior.on_send) {
    fate = network->behavior.on_send(network->behavior.ctx, to, size, data);
  }

  if (fate.lost) {
    ++network->stats.lost;
    return false;
  }

  enqueue(network, now, fate.delay, to, size, data);
  if (fate.duplicated) {
    ++network->stats.duplicated;
    enqueue(network, now, fate.duplicate_delay, to, size, data);
  }
  return true;
}

bool sim_network_receive(sim_network_t *network,
                         pcp_time_ms_t now,
                         sim_datagram_t *out) {
  if (network->in_flight == 0) {
    return false;
  }

  auto const next = next_datagram(network);
  if (network->datagrams[next].deliver_at > now) {
    return false;
  }

  *out = network->datagrams[next];
  memmove(&network->datagrams[next], &network->datagrams[next + 1],
          (network->in_flight - next - 1) * sizeof(sim_datagram_t));
  --network->in_flight;
  return true;
}

bool sim_network_next_arrival(sim_network_t const *network,
                              pcp_time_ms_t *out) {
  if (network->in_flight == 0) {
    return false;
  }

  *out = network->datagrams[next_datagram(network)].deliver_at;
  return true;
}

size_t sim_network_in_flight(sim_network_t const *network) {
  return network->in_flight;
}
