#include "wire.h"
#include <pcp/pcp.h>

#include <alloca.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void pcp_loop(pcp_session_t *session);
static bool pcp_rng(void * /*ctx*/, size_t count, uint8_t bytes[count]);

int main() {
  pcp_config_t config = {.local = {0},
                         .allocator = {0},
                         .rng = {
                             .ctx = nullptr,
                             .random = pcp_rng,
                         }};
  pcp_session_t *session = pcp_session_begin(config);

  auto const handle =
      pcp_session_map_request(session, 1024, UDP, 12345, 0, nullptr);
  if (!handle.has_value) {
    fprintf(stderr, "pcp_session_map_request: %s", strerror(handle.error));
    return EXIT_FAILURE;
  }

  pcp_loop(session);

  pcp_session_end(session);
  return EXIT_SUCCESS;
}

static void pcp_send(pcp_datagram_t const * /*packet*/) {}

static void pcp_loop(pcp_session_t *session) {
  pcp_datagram_t *datagram =
      alloca(sizeof(pcp_datagram_t) + PCP_PACKET_MAX_SIZE);
  *datagram = (pcp_datagram_t){
      .id = 0,
      .size = PCP_PACKET_MAX_SIZE,
  };

  while (true) {
    pcp_event_t event = {0};
    while (pcp_session_pop_event(session, time(nullptr), &event)) {
      switch (event.type) {
      case PCP_EVENT_WRITE: {
        while (pcp_session_pop_packet(session, datagram)) {
          pcp_send(datagram);
        }
        break;
      }
      case PCP_EVENT_RESPONSE:
        printf("Hooray! Got a response for: %lu", event.response.handle);
        break;
      }
    }
  }
}

static uint32_t pcp_rand() {
  static uint32_t next = 1;
  return next = next * 1103515245u + 12345u;
}

static bool pcp_rng(void * /*ctx*/, size_t count, uint8_t bytes[count]) {
  size_t i = 0;
  for (; i < count / 4; ++i) {
    uint32_t const next = pcp_rand();
    bytes[i * 4 + 0] = (uint8_t)(next >> 0);
    bytes[i * 4 + 1] = (uint8_t)(next >> 8);
    bytes[i * 4 + 2] = (uint8_t)(next >> 16);
    bytes[i * 4 + 3] = (uint8_t)(next >> 24);
  }

  uint32_t const next = pcp_rand();

  switch (count % 4) {
  case 3:
    bytes[i * 4 + 0] = (uint8_t)(next >> 0);
    bytes[i * 4 + 1] = (uint8_t)(next >> 8);
    bytes[i * 4 + 2] = (uint8_t)(next >> 16);
    break;
  case 2:
    bytes[i * 4 + 0] = (uint8_t)(next >> 0);
    bytes[i * 4 + 1] = (uint8_t)(next >> 8);
    break;
  case 1:
    bytes[i * 4 + 0] = (uint8_t)(next >> 0);
    break;
  case 0:
    break;
  }

  return true;
}
