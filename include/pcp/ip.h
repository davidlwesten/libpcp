#pragma once

#include <stddef.h>
#include <stdint.h>

struct in_addr;
struct in6_addr;
struct sockaddr_in;
struct sockaddr_in6;

typedef struct ip_address {
  uint8_t addr[16];
} ip_address_t;

static_assert(sizeof(ip_address_t) == 16 && alignof(ip_address_t) == 1);

static constexpr ip_address_t ip_address_any_v4 = {
    .addr = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0}};
static constexpr ip_address_t ip_address_loopback_v4 = {
    .addr = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 127, 0, 0, 1}};
static constexpr ip_address_t ip_address_any_v6 = {.addr = {0}};
static constexpr ip_address_t ip_address_loopback_v6 = {
    .addr = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};

/**
 * @brief Construct the IPv4 address a.b.c.d
 */
ip_address_t ip_address_from_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

/**
 * @brief Convert an IPv4 address from the sockets API
 */
ip_address_t ip_address_from_in_addr(const struct in_addr *addr);

/**
 * @brief Convert an IPv6 address from the sockets API
 */
ip_address_t ip_address_from_in6_addr(const struct in6_addr *addr);

typedef struct optional_ip_address {
  bool has_value;
  ip_address_t value;
} optional_ip_address_t;

/**
 * @brief Parse an IPv4 or IPv6 address, e.g. "192.0.2.1" or "2001:db8::1"
 *
 * @returns The address, or nothing if `ip` isn't one
 */
optional_ip_address_t ip_address_from_string(const char *ip);

static constexpr size_t IP_ADDRESS_STRING_LENGTH = 56;

/**
 * @brief Format an address as text, IPv4 in the usual dotted form
 *
 * @param[in]  size The size of `dst`, IP_ADDRESS_STRING_LENGTH is enough
 * @param[out] dst  Where the null-terminated text goes
 * @param[in]  src  The address to format
 *
 * @returns The length of the text, or 0 if `dst` is too small
 */
size_t
ip_address_to_string(size_t size, char dst[size], const ip_address_t *src);

/**
 * @brief Test whether IP is v4 or v6
 */
bool ip_address_is_ipv4(const ip_address_t *ip);

/**
 * @brief Describe an ip network with a prefix (CIDR)
 *
 * The prefix length counts bits of the full 128-bit address, so IPv4 prefixes
 * are offset by 96.
 */
typedef struct ip_network {
  ip_address_t address;
  uint8_t prefix;
} ip_network_t;

/**
 * @brief Build a network from a regular prefix length
 *
 * Example:
 *   ip_network_from_address(ip_address_from_octets(203, 0, 113, 0), 24)
 */
ip_network_t ip_network_from_address(ip_address_t address, uint8_t prefix);

typedef uint16_t port_t;

typedef struct socket_address {
  ip_address_t ip;
  port_t port;
} socket_address_t;

/**
 * @brief Convert an IPv4 socket address from the sockets API
 */
socket_address_t
socket_address_from_sockaddr_in(const struct sockaddr_in *addr);

/**
 * @brief Convert an IPv6 socket address from the sockets API
 */
socket_address_t
socket_address_from_sockaddr_in6(const struct sockaddr_in6 *addr);

typedef struct optional_socket_address {
  bool has_value;
  socket_address_t value;
} optional_socket_address_t;

/**
 * @brief Parse "a.b.c.d:port" or "[ipv6]:port"
 *
 * @returns The socket address, or nothing if `address` isn't in either form
 */
optional_socket_address_t socket_address_from_string(const char *address);

static constexpr size_t SOCKET_ADDRESS_STRING_LENGTH = 56;

/**
 * @brief Format a socket address as "a.b.c.d:port" or "[ipv6]:port"
 *
 * @param[in]  size The size of `dst`, SOCKET_ADDRESS_STRING_LENGTH is enough
 * @param[out] dst  Where the null-terminated text goes
 * @param[in]  src  The socket address to format
 *
 * @returns The length of the text, or 0 if `dst` is too small
 */
size_t socket_address_to_string(size_t size,
                                char dst[size],
                                const socket_address_t *src);

/**
 * @brief Compare two socket addresses like memcmp
 *
 * Only equality is meaningful, the order isn't.
 *
 * @returns 0 if they're equal
 */
int socket_address_cmp(socket_address_t const *lhs,
                       socket_address_t const *rhs);
