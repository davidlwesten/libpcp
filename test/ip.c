#include <pcp/ip.h>

#include <assert.h>
#include <inttypes.h>
#include <unity.h>

#include "runner.h"

#include <arpa/inet.h>
#include <netinet/in.h>

static void test_ip_address_from_in_addr(void) {
  struct in_addr addr = {.s_addr = htonl(INADDR_LOOPBACK)};
  const auto result = ip_address_from_in_addr(&addr);
  TEST_ASSERT_EQUAL_UINT8((INADDR_LOOPBACK >> 24) & 0xFF, result.addr[12]);
  TEST_ASSERT_EQUAL_UINT8((INADDR_LOOPBACK >> 16) & 0xFF, result.addr[13]);
  TEST_ASSERT_EQUAL_UINT8((INADDR_LOOPBACK >> 8) & 0xFF, result.addr[14]);
  TEST_ASSERT_EQUAL_UINT8((INADDR_LOOPBACK >> 0) & 0xFF, result.addr[15]);
}

static void test_ip_address_from_in6_addr(void) {
  struct in6_addr addr = IN6ADDR_LOOPBACK_INIT;
  const auto result = ip_address_from_in6_addr(&addr);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr.s6_addr, result.addr, 16);
}

static void test_ip_address_ntop_ipv4(void) {
  struct in_addr loopback = {0};
  assert(1 == inet_pton(AF_INET, "127.0.0.1", &loopback));
  auto ip = ip_address_from_in_addr(&loopback);

  char string[INET_ADDRSTRLEN + 1];
  TEST_ASSERT_TRUE(ip_address_ntop(sizeof(string), string, &ip));
  TEST_ASSERT_EQUAL_STRING("127.0.0.1", string);

  struct in_addr any = {0};
  ip = ip_address_from_in_addr(&any);
  TEST_ASSERT_TRUE(ip_address_ntop(sizeof(string), string, &ip));
  TEST_ASSERT_EQUAL_STRING("0.0.0.0", string);
}

static void test_ip_address_ntop_ipv6(void) {
  const struct in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
  auto ip = ip_address_from_in6_addr(&loopback);

  char string[INET6_ADDRSTRLEN + 1];
  TEST_ASSERT_TRUE(ip_address_ntop(sizeof(string), string, &ip));
  TEST_ASSERT_EQUAL_STRING("::1", string);

  const struct in6_addr any = IN6ADDR_ANY_INIT;
  ip = ip_address_from_in6_addr(&any);
  TEST_ASSERT_TRUE(ip_address_ntop(sizeof(string), string, &ip));
  TEST_ASSERT_EQUAL_STRING("::", string);
}

static void test_socket_address_ntop_ipv4(void) {
  const struct sockaddr_in address = {.sin_family = AF_INET,
                                      .sin_addr = htonl(INADDR_LOOPBACK),
                                      .sin_port = htons(12345)};
  auto const socket = socket_address_from_sockaddr_in(&address);

  char dst[128] = {0};
  TEST_ASSERT_EQUAL_size_t(15, socket_address_ntop(sizeof(dst), dst, &socket));
  TEST_ASSERT_EQUAL_STRING("127.0.0.1:12345", dst);
}

static void test_socket_address_ntop_ipv6(void) {
  const struct sockaddr_in6 address = {.sin6_family = AF_INET6,
                                       .sin6_addr = IN6ADDR_LOOPBACK_INIT,
                                       .sin6_port = htons(12345)};
  auto const socket = socket_address_from_sockaddr_in6(&address);

  {
    char dst[128] = {0};
    TEST_ASSERT_EQUAL_size_t(11,
                             socket_address_ntop(sizeof(dst), dst, &socket));
    TEST_ASSERT_EQUAL_STRING("[::1]:12345", dst);
  }

  {
    char dst[1] = {0};
    TEST_ASSERT_EQUAL_size_t(0, socket_address_ntop(sizeof(dst), dst, &socket));
    TEST_ASSERT_EQUAL_STRING("", dst);
  }
}

void ip_tests() {
  RUN_TEST(test_ip_address_from_in_addr);
  RUN_TEST(test_ip_address_from_in6_addr);
  RUN_TEST(test_ip_address_ntop_ipv4);
  RUN_TEST(test_ip_address_ntop_ipv6);
  RUN_TEST(test_socket_address_ntop_ipv4);
  RUN_TEST(test_socket_address_ntop_ipv6);
}
