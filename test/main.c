#include <unity.h>

void setUp() {}
void tearDown() {}

extern void ip_tests();
extern void heap_tests();

int main(void) {
  UNITY_BEGIN();

  heap_tests();
  ip_tests();

  return UNITY_END();
}
