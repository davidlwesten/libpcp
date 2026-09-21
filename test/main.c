#include <unity.h>

void setUp() {}
void tearDown() {}

extern void ip_tests();

int main(void) {
  UNITY_BEGIN();

  ip_tests();

  return UNITY_END();
}
