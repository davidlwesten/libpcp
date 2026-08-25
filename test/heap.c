#include <math.h>
#include <unity.h>

#include <array.h>

static int compare(const int *lhs, const int *rhs) { return *rhs - *lhs; }

void test_create_empty(void) {
  auto heap = array_create(int);

  TEST_ASSERT_NOT_NULL(heap);
  TEST_ASSERT_EQUAL_UINT(0, array_size(heap));

  array_destroy(heap);
}

void test_single_insert_peek(void) {
  int *heap = array_create(int);
  array_heap_pushr(heap, 100, (heap_compare_t)compare);

  TEST_ASSERT_EQUAL_UINT(1, array_size(heap));
  TEST_ASSERT_EQUAL_INT(100, *heap);

  array_destroy(heap);
}

void test_insert_maintains_max_at_root(void) {
  int *heap = array_create(int);

  constexpr int values[] = {10, 50, 5, 100, 30};
  constexpr auto n = sizeof(values) / sizeof(values[0]);
  for (size_t i = 0; i < n; ++i) {
    array_heap_push(heap, values[i], (heap_compare_t)compare);
  }

  TEST_ASSERT_EQUAL_INT(100, *heap);

  array_destroy(heap);
}

void test_extract_returns_descending_order(void) {
  int *heap = array_create(int);

  constexpr int values[] = {10, 50, 5, 100, 30, 1, 77};
  constexpr size_t n = sizeof(values) / sizeof(values[0]);
  for (size_t i = 0; i < n; ++i) {
    array_heap_push(heap, values[i], (heap_compare_t)compare);
  }

  constexpr int expected_sorted_desc[] = {100, 77, 50, 30, 10, 5, 1};

  static_assert(sizeof(values) == sizeof(expected_sorted_desc));

  for (size_t i = 0; i < n; ++i) {
    int value;
    array_heap_pop(heap, &value, (heap_compare_t)compare);
    TEST_ASSERT_EQUAL_INT((expected_sorted_desc[i]), value);
  }

  array_destroy(heap);
}

void test_heapify_unsorted_array(void) {
  constexpr int values[] = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
  constexpr size_t n = sizeof(values) / sizeof(values[0]);

  int *array = array_create(int);
  for (size_t i = 0; i < n; ++i) {
    array_push_back(array, values[i]);
  }

  array_heapify(array, (heap_compare_t)compare);

  TEST_ASSERT_EQUAL_size_t(n, array_size(array));
  TEST_ASSERT_EQUAL_INT(9, *array);

  array_destroy(array);
}

void test_heapify_already_sorted_ascending(void) {
  constexpr int values[] = {1, 2, 3, 4, 5, 6, 7, 8};
  constexpr size_t n = sizeof(values) / sizeof(values[0]);

  int *array = array_create(int);
  for (size_t i = 0; i < n; ++i) {
    array_push_back(array, values[i]);
  }

  array_heapify(array, (heap_compare_t)compare);

  TEST_ASSERT_EQUAL_INT(8, *array);

  array_destroy(array);
}

void test_heapify_already_sorted_descending(void) {
  constexpr int values[] = {8, 7, 6, 5, 4, 3, 2, 1};
  constexpr size_t n = sizeof(values) / sizeof(values[0]);

  int *array = array_create(int);
  for (size_t i = 0; i < n; ++i) {
    array_push_back(array, values[i]);
  }

  array_heapify(array, (heap_compare_t)compare);

  TEST_ASSERT_EQUAL_INT(8, *array);

  array_destroy(array);
}

void test_insert_extract_interleaved(void) {
  int *heap = array_create(int);

  array_heap_pushr(heap, 10, (heap_compare_t)compare);
  array_heap_pushr(heap, 20, (heap_compare_t)compare);

  int value;
  array_heap_pop(heap, &value, (heap_compare_t)compare);
  TEST_ASSERT_EQUAL_INT(20, value);

  array_heap_pushr(heap, 15, (heap_compare_t)compare);
  array_heap_pushr(heap, 5, (heap_compare_t)compare);

  TEST_ASSERT_EQUAL_INT64(15, *heap);

  array_heap_pop(heap, &value, (heap_compare_t)compare);
  TEST_ASSERT_EQUAL_INT(15, value);

  array_heap_pop(heap, &value, (heap_compare_t)compare);
  TEST_ASSERT_EQUAL_INT(10, value);

  array_heap_pop(heap, &value, (heap_compare_t)compare);
  TEST_ASSERT_EQUAL_INT(5, value);

  array_destroy(heap);
}

void test_heapify_empty_array(void) {
  int *heap = array_create(int);

  array_heapify(heap, (heap_compare_t)compare);
  TEST_ASSERT_EQUAL_size_t(0, array_size(heap));

  array_destroy(heap);
}

void test_heapify_single_element(void) {
  int *heap = array_create(int);

  array_push_backr(heap, 42);

  TEST_ASSERT_EQUAL_size_t(1, array_size(heap));
  TEST_ASSERT_EQUAL_INT(42, *heap);

  array_destroy(heap);
}

void heap_tests() {
  RUN_TEST(test_create_empty);
  RUN_TEST(test_single_insert_peek);
  RUN_TEST(test_insert_maintains_max_at_root);
  RUN_TEST(test_extract_returns_descending_order);
  RUN_TEST(test_heapify_unsorted_array);
  RUN_TEST(test_heapify_already_sorted_ascending);
  RUN_TEST(test_heapify_already_sorted_descending);
  RUN_TEST(test_insert_extract_interleaved);
  RUN_TEST(test_heapify_empty_array);
  RUN_TEST(test_heapify_single_element);
}
