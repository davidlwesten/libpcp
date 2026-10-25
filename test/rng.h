#pragma once

#include <pcp/pcp.h>

#include <string.h>

// Fills the output with copies of the uint16_t `ctx` points to, so every jitter
// drawn by the schedule is `value % 201 - 100` per mille.
//
// The schedule reads its random value by having the RNG write straight into a
// uint16_t, i.e. in the host's byte order. Real random bytes don't care about
// that, but these tests need an exact value back out, so the copies are made
// with memcpy in host byte order too instead of being assembled byte by byte
// in a fixed order. Otherwise e.g. 100 (no jitter) would read back as 25600 on
// a big-endian host.
static inline bool test_rng_fixed(void *ctx, size_t size, uint8_t data[size]) {
  for (size_t i = 0; i < size; i += sizeof(uint16_t)) {
    size_t const remaining = size - i;
    memcpy(data + i, ctx,
           remaining < sizeof(uint16_t) ? remaining : sizeof(uint16_t));
  }
  return true;
}

static inline bool
test_rng_failing(void * /*ctx*/, size_t /*size*/, uint8_t /*data*/[]) {
  return false;
}
