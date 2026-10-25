#pragma once

#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "ip.h"

typedef struct pcp_allocator {
  void *(*alloc)(void *ctx, size_t size);
  void (*free)(void *ctx, void *ptr);
  void *ctx;
} pcp_allocator_t;

typedef struct pcp_rng {
  bool (*random)(void *ctx, size_t size, uint8_t data[size]);
  void *ctx;
} pcp_rng_t;

typedef struct pcp_config {
  ip_address_t local;
  pcp_allocator_t allocator;
  pcp_rng_t rng;
} pcp_config_t;

typedef struct pcp_session pcp_session_t;

pcp_session_t *pcp_session_begin(pcp_config_t config);
void pcp_session_end(pcp_session_t *session);

typedef enum pcp_protocol : uint8_t {
  ALL = 0,
  TCP = 6,
  UDP = 17,
} pcp_protocol_t;

typedef enum pcp_opcode : uint8_t {
  ANNOUNCE = 0,
  MAP = 1,
  PEER = 2
} pcp_opcode_t;

typedef struct pcp_option {
  enum {
    THIRD_PARTY = 1,
    PREFER_FAILURE = 2,
    FILTER = 3,
  } type;
  union {
    struct {
      ip_address_t internal;
    } third_party;
    struct {
      socket_address_t peer;
      uint8_t prefix;
    } filter;
  };
} pcp_option_t;

typedef uintptr_t pcp_mapping_t;
typedef typeof(errno) errno_t;

typedef struct pcp_mapping_or_error {
  bool has_value;
  union {
    pcp_mapping_t value;
    errno_t error;
  };
} pcp_mapping_or_error_t;

/**
 * @brief Allocate a new map request
 *
 * @param[in] session  The session that owns this request
 * @param[in] lifetime The requested lifetime of the mapping
 * @param[in] protocol The protocol the mapping should affect (ALL, UDP, or TCP)
 * @param[in] port     The internal port for this mapping
 * @param[in] count    The number of options that should be included
 * @param[in] options  The option values to attach
 *
 * @returns The handle on success or an error otherwise
 */
pcp_mapping_or_error_t
pcp_session_map_request(pcp_session_t *session,
                        time_t lifetime,
                        pcp_protocol_t protocol,
                        port_t port,
                        size_t count,
                        pcp_option_t const options[count]);

/**
 * @brief Allocate a new peer request
 *
 * @param[in] session  The session that owns this request
 * @param[in] lifetime The requested lifetime of the mapping
 * @param[in] protocol The protocol the mapping should affect (ALL, UDP, or TCP)
 * @param[in] port     The internal port for the mapping
 * @param[in] peer     The peer address the server should use for the mapping
 * @param[in] count    The number of options that should be included
 * @param[in] options  The option values to attach
 *
 * @returns The handle on success or an error otherwise
 */
pcp_mapping_or_error_t
pcp_session_peer_request(pcp_session_t *session,
                         time_t lifetime,
                         pcp_protocol_t protocol,
                         port_t port,
                         socket_address_t peer,
                         size_t count,
                         pcp_option_t const options[count]);

typedef struct pcp_datagram {
  pcp_mapping_t id;
  size_t size;
  uint8_t data[];
} pcp_datagram_t;

/**
 * @brief Get session's next datagram
 *
 * @param[in] session The session's queue to interact with
 * @param[in] out     The destination buffer. Must not be nullptr
 *
 * @returns `false` if no more datagrams are available and `true` otherwise
 */
bool pcp_session_pop_packet(pcp_session_t *session, pcp_datagram_t *out);

typedef struct pcp_event {
  enum { PCP_EVENT_WRITE, PCP_EVENT_RESPONSE } type;
  union {
    struct {
      pcp_mapping_t handle;
    } response;
  };
} pcp_event_t;

/**
 * @brief Advance the session and get the next event
 *
 * @param[in] session The session to advance
 * @param[in] now     The current time
 * @param[in] event   The next event to handle. Must not be nullptr
 *
 * @returns `true` while events can be processed and `false` otherwise
 */
bool pcp_session_pop_event(pcp_session_t *session,
                           time_t now,
                           pcp_event_t *event);

/**
 * @brief Get the next time point when this session needs interaction
 *
 * @param[in] session The session which to query for a timeout
 * @param[in] out     The destination to which we can write
 *
 * @returns `true` when a timeout is available and `false` otherwise
 */
bool pcp_session_pop_timeout(pcp_session_t *session, time_t *out);

void pcp_session_handle_datagram(pcp_session_t *session,
                                 time_t now,
                                 pcp_datagram_t const *data);
void pcp_session_handle_timeout(pcp_session_t *session, time_t now);
