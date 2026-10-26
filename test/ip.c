#include <pcp/ip.h>

#include <assert.h>
#include <inttypes.h>
#include <unity.h>

#include "runner.h"

#include <arpa/inet.h>
#include <netinet/in.h>

static void test_ip_address_from_in_addr() {
  struct in_addr addr = {.s_addr = htonl(INADDR_LOOPBACK)};
  const auto result = ip_address_from_in_addr(&addr);
  TEST_ASSERT_EQUAL_UINT8((INADDR_LOOPBACK >> 24) & 0xFF, result.addr[12]);
  TEST_ASSERT_EQUAL_UINT8((INADDR_LOOPBACK >> 16) & 0xFF, result.addr[13]);
  TEST_ASSERT_EQUAL_UINT8((INADDR_LOOPBACK >> 8) & 0xFF, result.addr[14]);
  TEST_ASSERT_EQUAL_UINT8((INADDR_LOOPBACK >> 0) & 0xFF, result.addr[15]);
}

static void test_ip_address_from_in6_addr() {
  struct in6_addr addr = IN6ADDR_LOOPBACK_INIT;
  const auto result = ip_address_from_in6_addr(&addr);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr.s6_addr, result.addr, 16);
}

static void test_ip_address_ntop_ipv4() {
  struct in_addr loopback = {0};
  assert(1 == inet_pton(AF_INET, "127.0.0.1", &loopback));
  auto ip = ip_address_from_in_addr(&loopback);

  char string[INET_ADDRSTRLEN + 1];
  TEST_ASSERT_TRUE(ip_address_to_string(sizeof(string), string, &ip));
  TEST_ASSERT_EQUAL_STRING("127.0.0.1", string);

  struct in_addr any = {0};
  ip = ip_address_from_in_addr(&any);
  TEST_ASSERT_TRUE(ip_address_to_string(sizeof(string), string, &ip));
  TEST_ASSERT_EQUAL_STRING("0.0.0.0", string);
}

static void test_ip_address_ntop_ipv6() {
  const struct in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
  auto ip = ip_address_from_in6_addr(&loopback);

  char string[INET6_ADDRSTRLEN + 1];
  TEST_ASSERT_TRUE(ip_address_to_string(sizeof(string), string, &ip));
  TEST_ASSERT_EQUAL_STRING("::1", string);

  const struct in6_addr any = IN6ADDR_ANY_INIT;
  ip = ip_address_from_in6_addr(&any);
  TEST_ASSERT_TRUE(ip_address_to_string(sizeof(string), string, &ip));
  TEST_ASSERT_EQUAL_STRING("::", string);
}

static void test_socket_address_ntop_ipv4() {
  const struct sockaddr_in address = {.sin_family = AF_INET,
                                      .sin_addr = htonl(INADDR_LOOPBACK),
                                      .sin_port = htons(12345)};
  auto const socket = socket_address_from_sockaddr_in(&address);

  char dst[128] = {0};
  TEST_ASSERT_EQUAL_size_t(15,
                           socket_address_to_string(sizeof(dst), dst, &socket));
  TEST_ASSERT_EQUAL_STRING("127.0.0.1:12345", dst);
}

static void test_socket_address_ntop_ipv6() {
  const struct sockaddr_in6 address = {.sin6_family = AF_INET6,
                                       .sin6_addr = IN6ADDR_LOOPBACK_INIT,
                                       .sin6_port = htons(12345)};
  auto const socket = socket_address_from_sockaddr_in6(&address);

  {
    char dst[128] = {0};
    TEST_ASSERT_EQUAL_size_t(
        11, socket_address_to_string(sizeof(dst), dst, &socket));
    TEST_ASSERT_EQUAL_STRING("[::1]:12345", dst);
  }

  {
    char dst[1] = {0};
    TEST_ASSERT_EQUAL_size_t(
        0, socket_address_to_string(sizeof(dst), dst, &socket));
    TEST_ASSERT_EQUAL_STRING("", dst);
  }
}

// the constants must match what the system's own definitions convert to
static void test_ip_address_constants() {
  struct in_addr const any_v4 = {.s_addr = htonl(INADDR_ANY)};
  struct in_addr const loopback_v4 = {.s_addr = htonl(INADDR_LOOPBACK)};
  struct in6_addr const any_v6 = IN6ADDR_ANY_INIT;
  struct in6_addr const loopback_v6 = IN6ADDR_LOOPBACK_INIT;

  auto expected = ip_address_from_in_addr(&any_v4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.addr, ip_address_any_v4.addr, 16);
  expected = ip_address_from_in_addr(&loopback_v4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.addr, ip_address_loopback_v4.addr, 16);
  expected = ip_address_from_in6_addr(&any_v6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.addr, ip_address_any_v6.addr, 16);
  expected = ip_address_from_in6_addr(&loopback_v6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.addr, ip_address_loopback_v6.addr, 16);

  char string[INET6_ADDRSTRLEN + 1];
  TEST_ASSERT_TRUE(
      ip_address_to_string(sizeof(string), string, &ip_address_loopback_v4));
  TEST_ASSERT_EQUAL_STRING("127.0.0.1", string);
  TEST_ASSERT_TRUE(
      ip_address_to_string(sizeof(string), string, &ip_address_loopback_v6));
  TEST_ASSERT_EQUAL_STRING("::1", string);
}

static void test_ip_network_from_address() {
  // the usual CIDR prefix, stored for the 128-bit address
  auto network =
      ip_network_from_address(ip_address_from_octets(203, 0, 113, 0), 24);
  TEST_ASSERT_EQUAL_UINT8(120, network.prefix);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(ip_address_from_octets(203, 0, 113, 0).addr,
                                network.address.addr, 16);

  auto const v6 = ip_address_from_string("2001:db8::").value;
  network = ip_network_from_address(v6, 32);
  TEST_ASSERT_EQUAL_UINT8(32, network.prefix);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(v6.addr, network.address.addr, 16);
}

static void test_socket_address_from_string() {
  auto result = socket_address_from_string("192.168.1.1:5351");
  TEST_ASSERT_TRUE(result.has_value);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(ip_address_from_octets(192, 168, 1, 1).addr,
                                result.value.ip.addr, 16);
  TEST_ASSERT_EQUAL_UINT16(5351, result.value.port);

  result = socket_address_from_string("[2001:db8::1]:65535");
  TEST_ASSERT_TRUE(result.has_value);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      ip_address_from_string("2001:db8::1").value.addr, result.value.ip.addr,
      16);
  TEST_ASSERT_EQUAL_UINT16(65535, result.value.port);
}

static void test_socket_address_from_string_rejects_malformed() {
  char const *const malformed[] = {
      "",
      "1.2.3.4",
      "1.2.3.4:",       // no port
      ":5351",          // no address
      "[]:5351",        // no address
      "[::1]",          // no port
      "[::1]:",         // no port
      "[::1]x:5351",    // junk after the address
      "1.2.3.4:99999",  // port out of range
      "1.2.3.4:123456", // port too long
      "1.2.3.4:53x",    // junk after the port
      "1.2.3.4:-1",
      "1.2.3.4:5351:1",
      "2001:db8::1:5351", // IPv6 needs brackets
      "[1.2.3.4]:5351",   // IPv4 doesn't take brackets
      "a.b.c.d:5351",
      "1111111111111111111111111111111111111111111111111.2.3.4:5351",
  };

  for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
    TEST_ASSERT_FALSE_MESSAGE(
        socket_address_from_string(malformed[i]).has_value, malformed[i]);
  }
  TEST_ASSERT_FALSE(socket_address_from_string(nullptr).has_value);
}

void ip_tests() {
  RUN_TEST(test_ip_address_from_in_addr);
  RUN_TEST(test_ip_address_from_in6_addr);
  RUN_TEST(test_ip_address_ntop_ipv4);
  RUN_TEST(test_ip_address_ntop_ipv6);
  RUN_TEST(test_ip_address_constants);
  RUN_TEST(test_socket_address_ntop_ipv4);
  RUN_TEST(test_socket_address_ntop_ipv6);
  RUN_TEST(test_ip_network_from_address);
  RUN_TEST(test_socket_address_from_string);
  RUN_TEST(test_socket_address_from_string_rejects_malformed);
}
