#pragma once

#include <stddef.h>

#define array_create(T) _array_create(0, sizeof(T))
#define array_push_front(ARRAY, T) _array_push_front((void **)&ARRAY, &T)
#define array_push_frontr(ARRAY, T)                                            \
  do {                                                                         \
    auto const temp = (T);                                                     \
    array_push_front(ARRAY, temp, CMP);                                        \
  } while (0)
#define array_push_back(ARRAY, T) _array_push_back((void **)&ARRAY, &T)
#define array_push_backr(ARRAY, T)                                             \
  do {                                                                         \
    auto const temp = (T);                                                     \
    array_push_back(ARRAY, temp);                                              \
  } while (0)
#define array_heap_push(ARRAY, T, CMP)                                         \
  _array_heap_push((void **)&ARRAY, &T, CMP)
#define array_heap_pushr(ARRAY, T, CMP)                                        \
  do {                                                                         \
    auto const temp = (T);                                                     \
    array_heap_push(ARRAY, temp, CMP);                                         \
  } while (0)

[[nodiscard]] void *_array_create(size_t capacity, size_t stride);
size_t array_size(void *array);
size_t array_capacity(void *array);
size_t array_stride(void *array);
void *_array_at(void *array, size_t i);
void _array_push_front(void **array, void const *element);
void _array_push_back(void **array, void const *element);
void array_pop_front(void *array, void *element);
void array_pop_back(void *array, void *element);
void array_swap_index(void *array, size_t lhs, size_t rhs);
void array_swap(void *array, void *lhs, void *rhs);
void array_destroy(void *array);

typedef int (*heap_compare_t)(void const *lhs, void const *rhs);

void array_heapify(void *array, heap_compare_t compare);
void _array_heap_push(void **array,
                      void const *element,
                      heap_compare_t compare);
void array_heap_pop(void *array, void *element, heap_compare_t compare);
