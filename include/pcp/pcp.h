#pragma once

#include <errno.h>
#include <stdint.h>

#include "ip.h"

/**
 * @brief A point in time in milliseconds
 *
 * Must come from a monotonic clock (e.g. CLOCK_MONOTONIC), never wall-clock
 * time. The origin is up to the application; the library only compares and
 * subtracts these values.
 */
typedef uint64_t pcp_time_ms_t;

typedef struct pcp_optional_time_ms {
  bool has_value;
  pcp_time_ms_t value;
} pcp_optional_time_ms_t;

/**
 * @brief A duration in seconds, as carried on the wire (e.g. lifetimes)
 */
typedef uint32_t pcp_time_s_t;

typedef struct pcp_allocator {
  void *(*alloc)(void *ctx, size_t size);
  void (*free)(void *ctx, void *ptr);
  void *ctx;
} pcp_allocator_t;

typedef struct pcp_rng {
  bool (*random)(void *ctx, size_t size, uint8_t data[size]);
  void *ctx;
} pcp_rng_t;

typedef struct pcp_buffer {
  size_t size;
  uint8_t *data;
} pcp_buffer_t;

typedef struct pcp_config {
  ip_address_t local;
  pcp_allocator_t allocator;
  pcp_rng_t rng;
  /// Where outgoing packets are written, see PCP_EVENT_SEND. Must hold at least
  /// PCP_PACKET_MAX_SIZE bytes and stay alive until pcp_session_end. Every
  /// pcp_session_poll may overwrite it, so the application shouldn't use
  /// it for anything else
  pcp_buffer_t buffer;
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

typedef uintptr_t pcp_handle_t;
typedef typeof(errno) errno_t;

typedef struct pcp_handle_or_error {
  bool has_value;
  union {
    pcp_handle_t value;
    errno_t error;
  };
} pcp_handle_or_error_t;

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
 * The request is due immediately, so the application should poll the session
 * afterwards, see pcp_session_poll.
 *
 * @returns The handle on success or an error otherwise
 */
pcp_handle_or_error_t
pcp_session_map_request(pcp_session_t *session,
                        pcp_time_s_t lifetime,
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
 * The request is due immediately, so the application should poll the session
 * afterwards, see pcp_session_poll.
 *
 * @returns The handle on success or an error otherwise
 */
pcp_handle_or_error_t
pcp_session_peer_request(pcp_session_t *session,
                         pcp_time_s_t lifetime,
                         pcp_protocol_t protocol,
                         port_t port,
                         socket_address_t peer,
                         size_t count,
                         pcp_option_t const options[count]);

constexpr size_t PCP_PACKET_MAX_SIZE = 1100;

typedef struct pcp_event {
  enum {
    /// Send the request described by `send`
    PCP_EVENT_SEND,
    PCP_EVENT_RESPONSE,
    /// Nothing more to do before `timeout.deadline`, see pcp_session_poll
    PCP_EVENT_TIMEOUT,
  } type;
  union {
    /**
     * The request to send to the server
     *
     * `data` points into the buffer from pcp_config_t and is only valid until
     * the next call to pcp_session_poll, which may write the next request into
     * the same buffer. The application should send it before polling again. To
     * send it later (e.g. retrying after send() failed with EAGAIN), copy it
     * first. Otherwise just drop it: the session already scheduled a
     * retransmission when it returned this event.
     */
    struct {
      pcp_handle_t handle;
      size_t size;
      uint8_t const *data;
    } send;
    struct {
      pcp_handle_t handle;
    } response;
    struct {
      /// Always in the future of the `now` passed to pcp_session_poll. Empty if
      /// nothing is scheduled, i.e. the application can wait indefinitely
      pcp_optional_time_ms_t deadline;
    } timeout;
  };
} pcp_event_t;

/**
 * @brief Advance the session and get the next event
 *
 * This drives the session. Call it until it returns PCP_EVENT_TIMEOUT whenever
 * the application wakes up: when a datagram arrived, the previous deadline
 * passed, or a request was made. Then wait until `timeout.deadline` (or a
 * datagram arrives) before polling again.
 *
 * Each event is only returned once. Polling invalidates the data of the
 * previous event, see pcp_event_t::send.
 *
 * @param[in] session The session to advance
 * @param[in] now     The current time
 *
 * @returns The next event to handle
 */
pcp_event_t pcp_session_poll(pcp_session_t *session, pcp_time_ms_t now);

void pcp_session_push_datagram(pcp_session_t *session,
                               pcp_time_ms_t now,
                               size_t size,
                               uint8_t const data[size]);
