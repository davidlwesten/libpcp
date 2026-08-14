#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

extern void ip_tests(void);

int main(void) {
  UNITY_BEGIN();

  ip_tests();

  return UNITY_END();
}
