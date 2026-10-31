#pragma once

#include <errno.h>
#include <stdint.h>

#include "ip.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A point in time in milliseconds
 *
 * Has to come from a monotonic clock like CLOCK_MONOTONIC, not wall-clock time.
 * Any origin works, only differences matter.
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
  bool (*fill)(void *ctx, size_t size, uint8_t data[]);
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
  /// Outgoing packets are written here (see PCP_EVENT_SEND). Needs at least
  /// PCP_PACKET_MAX_SIZE bytes and has to outlive the session. Don't use it for
  /// anything else, any poll can overwrite it.
  pcp_buffer_t buffer;
} pcp_config_t;

typedef struct pcp_session pcp_session_t;

/**
 * @brief Create a session
 *
 * `config.rng` is required. If `config.allocator` is left zeroed, malloc and
 * free are used.
 *
 * @param[in] config The session's configuration, copied into the session
 *
 * @returns The session, or nullptr if it couldn't be allocated
 */
pcp_session_t *pcp_session_begin(pcp_config_t config);

/**
 * @brief Free the session and its mappings
 *
 * Doesn't tell the server, so mappings stay around until their leases run out.
 * For a clean shutdown, release all mappings and keep polling until
 * PCP_EVENT_TIMEOUT has no deadline.
 */
void pcp_session_end(pcp_session_t *session);

typedef enum pcp_protocol : uint8_t {
  ALL = 0,
  TCP = 6,
  UDP = 17,
} pcp_protocol_t;

typedef struct pcp_option {
  enum {
    /// Map on behalf of another internal host (MAP and PEER)
    THIRD_PARTY = 1,
    /// Only allow traffic from `filter.peers` (MAP only). Filters add up across
    /// requests, renewals included, and a prefix of 0 clears them. Not sent
    /// when releasing.
    FILTER = 3,
  } type;
  union {
    struct {
      ip_address_t internal;
    } third_party;
    struct {
      /// See ip_network_from_address
      ip_network_t peers;
      /// 0 for any port
      port_t port;
    } filter;
  };
} pcp_option_t;

typedef uint16_t pcp_handle_t;
typedef int errno_t;

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
 * @param[in] lifetime The requested lifetime of the mapping (not 0)
 * @param[in] protocol The protocol the mapping should affect (ALL, UDP, or TCP)
 * @param[in] port     The internal port for this mapping
 * @param[in] count    The number of options that should be included
 * @param[in] options  The option values to attach
 *
 * The request is due immediately, so the application should poll the session
 * afterwards, see pcp_session_poll.
 *
 * @returns The handle, or an error (EMSGSIZE if the options don't fit in a
 *          packet)
 */
pcp_handle_or_error_t pcp_session_map_request(pcp_session_t *session,
                                              pcp_time_s_t lifetime,
                                              pcp_protocol_t protocol,
                                              port_t port,
                                              size_t count,
                                              pcp_option_t const options[]);

/**
 * @brief Allocate a new peer request
 *
 * @param[in] session  The session that owns this request
 * @param[in] lifetime The requested lifetime of the mapping (not 0)
 * @param[in] protocol The protocol the mapping should affect (ALL, UDP, or TCP)
 * @param[in] port     The internal port for the mapping
 * @param[in] peer     The peer address the server should use for the mapping
 * @param[in] count    The number of options that should be included
 * @param[in] options  The option values to attach
 *
 * The request is due immediately, so the application should poll the session
 * afterwards, see pcp_session_poll.
 *
 * @returns The handle, or an error (EMSGSIZE if the options don't fit in a
 *          packet)
 */
pcp_handle_or_error_t pcp_session_peer_request(pcp_session_t *session,
                                               pcp_time_s_t lifetime,
                                               pcp_protocol_t protocol,
                                               port_t port,
                                               socket_address_t peer,
                                               size_t count,
                                               pcp_option_t const options[]);

/**
 * @brief Release a mapping the application no longer needs
 *
 * MAP mappings get a delete request, so poll afterwards. PEER mappings can't be
 * deleted and just expire.
 *
 * The handle is invalid after this and no more events will refer to it.
 *
 * @param[in] session The session that owns the mapping
 * @param[in] handle  The mapping to release. Ignored if it was already
 *                    released or failed.
 */
void pcp_session_release(pcp_session_t *session, pcp_handle_t handle);

constexpr size_t PCP_PACKET_MAX_SIZE = 1100;

typedef enum pcp_result : uint8_t {
  SUCCESS = 0,
  UNSUPP_VERSION = 1,
  NOT_AUTHORIZED = 2,
  MALFORMED_REQUEST = 3,
  UNSUPP_OPCODE = 4,
  UNSUPP_OPTION = 5,
  MALFORMED_OPTION = 6,
  NETWORK_FAILURE = 7,
  NO_RESOURCES = 8,
  UNSUPP_PROTOCOL = 9,
  USER_EX_QUOTA = 10,
  CANNOT_PROVIDE_EXTERNAL = 11,
  ADDRESS_MISMATCH = 12,
  EXCESSIVE_REMOTE_PEERS = 13,
} pcp_result_t;

/**
 * @brief Something the application has to handle, see pcp_session_poll
 *
 * The address from PCP_EVENT_MAPPED is valid until the next PCP_EVENT_UNMAPPED
 * or PCP_EVENT_FAILED for the same handle.
 */
typedef struct pcp_event {
  enum {
    /// Send the request described by `send`
    PCP_EVENT_SEND,
    /// The mapping is reachable at `mapped.external`. Sent again if the
    /// address changes.
    PCP_EVENT_MAPPED,
    /// The mapping was lost. It gets requested again and PCP_EVENT_MAPPED
    /// follows once that works.
    PCP_EVENT_UNMAPPED,
    /// The server refused the mapping. This is the last event for the handle,
    /// which is invalid afterwards.
    PCP_EVENT_FAILED,
    /// Nothing more to do before `timeout.deadline`, see pcp_session_poll
    PCP_EVENT_TIMEOUT,
  } type;
  union {
    struct {
      size_t size;
      /// Points into the buffer from pcp_config_t and is only valid until the
      /// next poll. Copy it if you want to send it later. If sending fails,
      /// it's fine to just drop it, the retransmission is already scheduled.
      uint8_t const *data;
    } send;
    struct {
      pcp_handle_t handle;
      socket_address_t external;
    } mapped;
    struct {
      pcp_handle_t handle;
    } unmapped;
    struct {
      pcp_handle_t handle;
      pcp_result_t result;
    } failed;
    struct {
      /// Always later than `now`. Empty if nothing is scheduled, in which case
      /// just wait for the next datagram.
      pcp_optional_time_ms_t deadline;
    } timeout;
  };
} pcp_event_t;

/**
 * @brief Advance the session and get the next event
 *
 * Call this until it returns PCP_EVENT_TIMEOUT whenever something happens: a
 * datagram arrived, the deadline passed or a request was made. Then wait until
 * `timeout.deadline` or the next datagram.
 *
 * Every call invalidates the previous event's data (see pcp_event_t::send).
 *
 * @param[in] session The session to advance
 * @param[in] now     The current time
 *
 * @returns The next event to handle
 */
pcp_event_t pcp_session_poll(pcp_session_t *session, pcp_time_ms_t now);

/**
 * @brief Hand a datagram received from the PCP server to the session
 *
 * The application should poll the session afterwards, see pcp_session_poll.
 *
 * @param[in] session The session to update
 * @param[in] now     The current time
 * @param[in] size    The size of `data`
 * @param[in] data    The datagram's payload
 */
void pcp_session_on_datagram(pcp_session_t *session,
                             pcp_time_ms_t now,
                             size_t size,
                             uint8_t const data[]);

#ifdef __cplusplus
}
#endif
