#pragma once

#include <stddef.h>

typedef struct pcp_allocator {
  void *(*alloc)(void *ctx, size_t size);
  void (*free)(void *ctx, void *ptr);
  void *ctx;
} pcp_allocator_t;

pcp_allocator_t pcp_allocator_default();
pcp_allocator_t pcp_allocator_arena(void *buffer, size_t capacity);
