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
    .addr = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 1, 0, 0, 127}};
static constexpr ip_address_t ip_address_any_v6 = {.addr = {0}};

ip_address_t ip_address_from_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
ip_address_t ip_address_from_in_addr(const struct in_addr *addr);
ip_address_t ip_address_from_in6_addr(const struct in6_addr *addr);

typedef struct optional_ip_address {
  bool has_value;
  ip_address_t value;
} optional_ip_address_t;

optional_ip_address_t ip_address_from_string(const char *ip);
size_t ip_address_ntop(size_t size, char dst[size], const ip_address_t *src);

typedef uint16_t port_t;

typedef struct socket_address {
  ip_address_t ip;
  port_t port;
} socket_address_t;

socket_address_t
socket_address_from_sockaddr_in(const struct sockaddr_in *addr);
socket_address_t
socket_address_from_sockaddr_in6(const struct sockaddr_in6 *addr);

typedef struct optional_socket_address {
  bool has_value;
  socket_address_t address;
} optional_socket_address_t;

optional_socket_address_t socket_address_from_string(const char *address);
size_t
socket_address_ntop(size_t size, char dst[size], const socket_address_t *src);
