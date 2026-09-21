#include <netinet/in.h>
#include <pcp/ip.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <netdb.h>

#include "types.h"

constexpr char IPV4_MAPPED_PREFIX[] = "\0\0\0\0\0\0\0\0\0\0\xFF\xFF";

static size_t min(size_t a, size_t b) { return a < b ? a : b; }

static bool ip_address_is_ipv4_mapped(const ip_address_t *ip) {
  assert(ip);
  return memcmp(ip->addr, IPV4_MAPPED_PREFIX, 12) == 0;
}

static struct in_addr ip_address_to_in_addr(const ip_address_t *ip) {
  assert(ip);
  assert(ip_address_is_ipv4_mapped(ip));
  struct in_addr result = {0};
  memcpy(&result.s_addr, ip->addr + 12, sizeof(result.s_addr));
  return result;
}

static struct in6_addr ip_address_to_in6_addr(const ip_address_t *ip) {
  assert(ip);
  assert(!ip_address_is_ipv4_mapped(ip));
  struct in6_addr result = {0};
  memcpy(result.s6_addr, ip->addr, sizeof(result.s6_addr));
  return result;
}

ip_address_t
ip_address_from_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (ip_address_t){
      .addr = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, a, b, c, d}};
}

ip_address_t ip_address_from_in_addr(const struct in_addr *addr) {
  assert(addr);
  // construct ipv4-mapped ipv6 address (80 bits of 0s + 16 bits of 1s + 32 bits
  // of ipv4)
  ip_address_t result = {0};
  memcpy(result.addr, IPV4_MAPPED_PREFIX, 12);
  memcpy(result.addr + 12, &addr->s_addr, sizeof(addr->s_addr));
  return result;
}

ip_address_t ip_address_from_in6_addr(const struct in6_addr *addr) {
  assert(addr);
  ip_address_t result = {0};
  static_assert(sizeof(result.addr) == sizeof(addr->s6_addr));
  memcpy(result.addr, addr->s6_addr, sizeof(result.addr));
  return result;
}

optional_ip_address_t ip_address_from_string(const char *ip) {
  struct in_addr ipv4 = {0};
  if (1 == inet_pton(AF_INET, ip, &ipv4)) {
    return (optional_ip_address_t){.has_value = true,
                                   .value = ip_address_from_in_addr(&ipv4)};
  }

  struct in6_addr ipv6 = {0};
  if (1 == inet_pton(AF_INET6, ip, &ipv6)) {
    return (optional_ip_address_t){.has_value = true,
                                   .value = ip_address_from_in6_addr(&ipv6)};
  }

  return (optional_ip_address_t){.has_value = false, .value = {}};
}

static pcp_transport_size_or_error_t
ip_address_ntop_v4(size_t size, char dst[size], const ip_address_t *src) {
  if (size < (INET_ADDRSTRLEN + 1)) {
    return (pcp_transport_size_or_error_t){.has_value = false, .error = ENOBUFS};
  }

  memset(dst, 0, INET_ADDRSTRLEN + 1);

  struct in_addr addr = ip_address_to_in_addr(src);
  if (!inet_ntop(AF_INET, &addr, dst, (socklen_t)size)) {
    return (pcp_transport_size_or_error_t){.has_value = false, .error = errno};
  }

  return (pcp_transport_size_or_error_t){.has_value = true, .value = strlen(dst)};
}

static pcp_transport_size_or_error_t
ip_address_ntop_v6(size_t size, char dst[size], const ip_address_t *src) {
  if (size < (INET6_ADDRSTRLEN + 1)) {
    return (pcp_transport_size_or_error_t){.has_value = false, .error = ENOBUFS};
  }

  memset(dst, 0, INET6_ADDRSTRLEN + 1);

  struct in6_addr addr = ip_address_to_in6_addr(src);
  if (!inet_ntop(AF_INET6, &addr, dst, (socklen_t)size)) {
    return (pcp_transport_size_or_error_t){.has_value = false, .error = errno};
  }

  return (pcp_transport_size_or_error_t){.has_value = true, .value = strlen(dst)};
}

size_t ip_address_ntop(size_t size, char dst[size], const ip_address_t *src) {
  assert(src);

  pcp_transport_size_or_error_t result;
  if (ip_address_is_ipv4_mapped(src)) {
    result = ip_address_ntop_v4(size, dst, src);
  } else {
    result = ip_address_ntop_v6(size, dst, src);
  }

  return result.has_value ? result.value : 0;
}

int ip_adddress_cmp(ip_address_t const *lhs, ip_address_t const *rhs) {
  return memcmp(lhs, rhs, sizeof(ip_address_t));
}

socket_address_t
socket_address_from_sockaddr_in(const struct sockaddr_in *addr) {
  return (socket_address_t){.ip = ip_address_from_in_addr(&addr->sin_addr),
                            .port = ntohs(addr->sin_port)};
}

socket_address_t
socket_address_from_sockaddr_in6(const struct sockaddr_in6 *addr) {
  return (socket_address_t){.ip = ip_address_from_in6_addr(&addr->sin6_addr),
                            .port = ntohs(addr->sin6_port)};
}

static bool extract_node_and_service(const char *src,
                                     char node[INET6_ADDRSTRLEN],
                                     char service[6]) {
  assert(src);
  assert(strlen(src) >= 3);

  const char *begin = nullptr;
  const char *end = nullptr;

  switch (src[0]) {
  case '[':
    begin = src + 1;
    end = strrchr(begin, ']');
    if (!end) {
      return false;
    }
    break;
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    begin = src;
    end = strrchr(begin, ':');
    if (!end) {
      return false;
    }
    break;
  default:
    return false;
  }

  assert(end > begin);
  const size_t ip_length = (size_t)(end - begin);
  strncpy(node, begin, min(ip_length, INET6_ADDRSTRLEN));

  // find ':'
  begin = strchr(end, ':');
  if (!begin) {
    return false;
  }

  // skip ':' and find new end
  begin = begin + 1;
  end = strchr(begin, '\0');

  assert(end > begin);
  const size_t port_length = (size_t)(end - begin);
  strncpy(service, begin, min(port_length, 6));

  return true;
}

optional_socket_address_t socket_address_from_string(const char *address) {
  if (!address || strlen(address) < 3) {
    return (optional_socket_address_t){.has_value = false, .address = {}};
  }

  char node[INET6_ADDRSTRLEN + 1] = {0};
  char service[7] = {0};
  if (!extract_node_and_service(address, node, service)) {
    return (optional_socket_address_t){.has_value = false, .address = {}};
  }

  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
  hints.ai_socktype = SOCK_DGRAM; // specify to constrain to one result
  struct addrinfo *result = nullptr;
  if (0 != getaddrinfo(node, service, &hints, &result)) {
    return (optional_socket_address_t){.has_value = false, .address = {}};
  }

  auto const socket_address =
      result->ai_family == AF_INET
          ? socket_address_from_sockaddr_in(
                (const struct sockaddr_in *)result->ai_addr)
          : socket_address_from_sockaddr_in6(
                (const struct sockaddr_in6 *)result->ai_addr);

  freeaddrinfo(result);

  return (optional_socket_address_t){.has_value = true,
                                     .address = socket_address};
}

static pcp_transport_size_or_error_t socket_address_ntop_v4(size_t size,
                                              char dst[size],
                                              const socket_address_t *address) {
  // need room for INET_ADDSTRLEN + ':xxxxx' + \0;
  if (size < (INET_ADDRSTRLEN + 7)) {
    return (pcp_transport_size_or_error_t){.has_value = false, .error = ENOBUFS};
  }

  auto const ip_size = ip_address_ntop_v4(size, dst, &address->ip);
  if (!ip_size.has_value) {
    return ip_size;
  }

  size -= ip_size.value;

  int const result = snprintf(dst + ip_size.value, size, ":%hu", address->port);
  if (result < 0) {
    return (pcp_transport_size_or_error_t){.has_value = false, .error = errno};
  }

  return (pcp_transport_size_or_error_t){.has_value = true,
                           .value = ip_size.value + (size_t)result};
}

static pcp_transport_size_or_error_t socket_address_ntop_v6(size_t size,
                                              char dst[size],
                                              const socket_address_t *address) {
  // need room for '[' + INET6_ADDRSTRLEN + ']:xxxxx' + '\0';
  constexpr size_t MIN_SIZE = INET6_ADDRSTRLEN + 9;
  if (size < MIN_SIZE) {
    return (pcp_transport_size_or_error_t){.has_value = false, .error = ENOBUFS};
  }

  dst[0] = '[';

  pcp_transport_size_or_error_t const ip_size =
      ip_address_ntop_v6(INET6_ADDRSTRLEN + 1, dst + 1, &address->ip);
  if (!ip_size.has_value) {
    return ip_size;
  }

  size -= ip_size.value + 1;

  int const result =
      snprintf(dst + ip_size.value + 1, size, "]:%hu", address->port);
  if (result < 0) {
    return (pcp_transport_size_or_error_t){.has_value = false, .error = errno};
  }

  return (pcp_transport_size_or_error_t){.has_value = true,
                           .value = ip_size.value + 1 + (size_t)result};
}

size_t socket_address_ntop(size_t size,
                           char dst[size],
                           const socket_address_t *address) {

  auto const result = ip_address_is_ipv4_mapped(&address->ip)
                          ? socket_address_ntop_v4(size, dst, address)
                          : socket_address_ntop_v6(size, dst, address);

  return result.has_value ? result.value : 0;
}

int socket_address_cmp(socket_address_t const *lhs,
                       socket_address_t const *rhs) {
  return memcmp(lhs, rhs, sizeof(socket_address_t));
}
