#include <pcp/allocator.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void *pcp_allocator_malloc(void * /*ctx*/, size_t size) {
  return malloc(size);
}

static void pcp_allocator_free(void * /*ctx*/, void *ptr) { free(ptr); }

typedef struct pcp_arena_header {
  size_t capacity;
  size_t used;
} pcp_arena_header_t;

static void *pcp_arena_alloc(void *ctx, size_t size) {
  pcp_arena_header_t *arena = ctx;
  if (!arena || arena->used > arena->capacity) {
    return nullptr;
  }

  constexpr size_t alignment = alignof(max_align_t);
  unsigned char *const buffer = (unsigned char *)(arena + 1);
  uintptr_t const address = (uintptr_t)buffer + arena->used;
  size_t const padding =
      (alignment - (size_t)(address % alignment)) % alignment;
  if (padding > arena->capacity - arena->used) {
    return nullptr;
  }

  size_t const offset = arena->used + padding;
  if (size > arena->capacity - offset) {
    return nullptr;
  }

  arena->used = offset + size;
  return buffer + offset;
}

static void pcp_arena_free(void * /*ctx*/, void * /*ptr*/) {}

pcp_allocator_t pcp_allocator_default() {
  return (pcp_allocator_t){.alloc = pcp_allocator_malloc,
                           .free = pcp_allocator_free,
                           .ctx = nullptr};
}

pcp_allocator_t pcp_allocator_arena(void *buffer, size_t capacity) {
  if (!buffer) {
    return (pcp_allocator_t){.alloc = pcp_arena_alloc,
                             .free = pcp_arena_free,
                             .ctx = nullptr};
  }

  constexpr size_t alignment = alignof(pcp_arena_header_t);
  uintptr_t const address = (uintptr_t)buffer;
  size_t const offset = (alignment - (size_t)(address % alignment)) % alignment;
  if (offset > capacity || sizeof(pcp_arena_header_t) > capacity - offset) {
    return (pcp_allocator_t){.alloc = pcp_arena_alloc,
                             .free = pcp_arena_free,
                             .ctx = nullptr};
  }

  pcp_arena_header_t *const header =
      (pcp_arena_header_t *)((unsigned char *)buffer + offset);
  *header =
      (pcp_arena_header_t){.capacity = capacity - offset - sizeof(*header),
                           .used = 0};
  return (pcp_allocator_t){.alloc = pcp_arena_alloc,
                           .free = pcp_arena_free,
                           .ctx = header};
}
