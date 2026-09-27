#include "runner.h"

#include <string.h>

void setUp() {}
void tearDown() {}

extern void ip_tests();
extern void allocator_tests();
extern void lease_tests();
extern void test_transport_tests();

static const char *selected_test;
static size_t matched_tests;

bool test_runner_init(int argc, char *argv[]) {
  if (argc > 2) {
    return false;
  }
  selected_test = argc == 2 ? argv[1] : nullptr;
  matched_tests = 0;
  return true;
}

void test_runner_run(UnityTestFunction function, const char *name, int line) {
  if (selected_test && strcmp(selected_test, name) != 0) {
    return;
  }

  ++matched_tests;
  UnityDefaultTestRun(function, name, line);
}

bool test_runner_selected_test_found() {
  return !selected_test || matched_tests == 1;
}

int main(int argc, char *argv[]) {
  if (!test_runner_init(argc, argv)) {
    return 2;
  }

  UNITY_BEGIN();

  ip_tests();
  allocator_tests();
  test_transport_tests();
  lease_tests();

  int const result = UNITY_END();
  return test_runner_selected_test_found() ? result : 2;
}
