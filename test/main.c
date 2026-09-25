#include <unity.h>

void setUp() {}
void tearDown() {}

extern void ip_tests();
extern void test_transport_tests();

int main(void) {
  UNITY_BEGIN();

  ip_tests();
  test_transport_tests();

  return UNITY_END();
}
