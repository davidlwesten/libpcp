#include "array.h"

#include <assert.h>
#include <inttypes.h>
#include <malloc.h>
#include <string.h>

constexpr size_t HEADER_SIZE = 3 * sizeof(size_t);
enum header_index { CAPACITY = 0, SIZE = 1, STRIDE = 2, DATA = 3 };

#define HEADER(ARRAY) (((size_t *)ARRAY) - DATA)

static void *_array_grow(void *array, size_t new_capacity) {
  assert(array);
  assert(new_capacity);
  size_t *header = HEADER(array);
  assert(header[CAPACITY] < new_capacity);

  header[CAPACITY] = new_capacity;
  header[SIZE] = header[SIZE] < new_capacity ? header[SIZE] : new_capacity;
  header = realloc(header, HEADER_SIZE + (new_capacity * header[STRIDE]));

  return header + DATA;
}

static void *array_back(void *array) {
  assert(array);
  return (char *)array + (HEADER(array)[SIZE] * HEADER(array)[STRIDE]);
}

void *_array_create(size_t capacity, size_t stride) {
  assert(stride);
  size_t *const header = (size_t *)malloc(HEADER_SIZE + (capacity * stride));
  if (!header) {
    return nullptr;
  }

  header[CAPACITY] = capacity;
  header[SIZE] = 0;
  header[STRIDE] = stride;
  return header + DATA;
}

size_t array_size(void *array) {
  assert(array);
  return HEADER(array)[SIZE];
}

size_t array_capacity(void *array) {
  assert(array);
  return HEADER(array)[CAPACITY];
}

size_t array_stride(void *array) {
  assert(array);
  return HEADER(array)[STRIDE];
}

void *_array_at(void *array, size_t i) {
  assert(array);
  assert(i < array_size(array));
  return (char *)array + (i * HEADER(array)[STRIDE]);
}

void _array_push_front(void **array, void const *element) {
  assert(array && *array);
  auto header = HEADER(*array);
  if (header[CAPACITY] == header[SIZE]) {
    *array = _array_grow(*array, 2 * header[CAPACITY]);
  }

  size_t const old_size = header[SIZE]++;
  memmove((char *)*array + header[STRIDE], *array, old_size * header[STRIDE]);
  memcpy((char *)*array, element, header[STRIDE]);
}

#define MAX(A, B) ((A) > (B) ? (A) : (B))

void _array_push_back(void **array, void const *element) {
  assert(array && *array);
  auto header = HEADER(*array);
  if (header[CAPACITY] == header[SIZE]) {
    *array = _array_grow(*array, MAX(1, 2 * header[CAPACITY]));
    header = HEADER(*array);
  }

  size_t const old_size = header[SIZE]++;
  memcpy((char *)*array + (old_size * header[STRIDE]), element, header[STRIDE]);
}

void array_pop_front(void *array, void *element) {
  assert(array);
  auto header = HEADER(array);
  assert(header[SIZE] > 0);
  size_t const new_size = --header[SIZE];
  memcpy(element, array, header[STRIDE]);
  memmove(array, (char *)array + header[STRIDE], new_size * header[STRIDE]);
}

void array_pop_back(void *array, void *element) {
  assert(array);
  auto header = HEADER(array);
  assert(header[SIZE] > 0);
  size_t const new_size = --header[SIZE];
  memcpy(element, (char *)array + (new_size * header[STRIDE]), header[STRIDE]);
}

void array_swap_index(void *array, size_t lhs, size_t rhs) {
  assert(array);
  assert(lhs < HEADER(array)[SIZE]);
  assert(rhs < HEADER(array)[SIZE]);
  array_swap(array, _array_at(array, lhs), _array_at(array, rhs));
}

void array_swap(void *array, void *lhs, void *rhs) {
  assert(array);
  assert(lhs < array_back(array));
  assert(rhs < array_back(array));

  size_t const stride = HEADER(array)[STRIDE];
  char buffer[stride] = {};

  memcpy(buffer, lhs, stride);
  memcpy(lhs, rhs, stride);
  memcpy(rhs, buffer, stride);
}

void array_destroy(void *array) {
  assert(array);
  free(HEADER(array));
}

static size_t heap_parent(size_t index) { return (index - 1) / 2; }
static size_t heap_child_left(size_t index) { return 2 * index + 1; }
static size_t heap_child_right(size_t index) { return 2 * index + 2; }

void _array_heap_push(void **array,
                      void const *element,
                      heap_compare_t compare) {
  assert(array && *array);
  assert(element);
  assert(compare);
  _array_push_back(array, element);

  for (size_t i = HEADER(*array)[SIZE] - 1; i > 0; i = heap_parent(i)) {
    size_t const parent = heap_parent(i);
    if (compare(_array_at(*array, i), _array_at(*array, parent)) < 0) {
      array_swap_index(*array, parent, i);
    } else {
      break;
    }
  }
}

static void heap_sift_down(void *array, size_t root, heap_compare_t compare) {
  assert(array);
  assert(compare);

  size_t const size = array_size(array);
  assert(root < size);

  for (;;) {
    size_t smallest = root;
    size_t const left = heap_child_left(smallest);
    size_t const right = heap_child_right(smallest);

    if (left < size &&
        compare(_array_at(array, left), _array_at(array, smallest)) < 0) {
      smallest = left;
    }

    if (right < size &&
        compare(_array_at(array, right), _array_at(array, smallest)) < 0) {
      smallest = right;
    }

    if (root == smallest) {
      break;
    }

    array_swap_index(array, root, smallest);
    root = smallest;
  }
}

void array_heap_pop(void *array, void *element, heap_compare_t compare) {
  assert(array);
  assert(element);
  assert(compare);
  array_pop_front(array, element);
  if (array_size(array)) {
    heap_sift_down(array, 0, compare);
  }
}

void array_heapify(void *array, heap_compare_t compare) {
  size_t const size = array_size(array);
  if (size < 2) {
    return;
  }

  for (size_t node = (size / 2) - 1; node > 0; --node) {
    heap_sift_down(array, node, compare);
  }
  heap_sift_down(array, 0, compare);
}
