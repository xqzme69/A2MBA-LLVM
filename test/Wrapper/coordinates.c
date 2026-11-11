// clang-format off
// REQUIRES: clang, host-executable, a2mba-wrapper, x86-registered-target
// RUN: %a2mba_wrapper --hybrid ir --hybrid-region rotated-core --level heavy --seed 7 --stats -O2 "%s" -o "%t.exe" 2>&1 | %FileCheck "%s" --check-prefix=REGIONS
// RUN: "%t.exe"
// RUN: %a2mba_wrapper --hybrid ir --hybrid-region rotated-core --hybrid-layers context --level heavy --seed 7 --stats -O2 "%s" -o "%t.exe" 2>&1 | %FileCheck "%s" --check-prefix=CONTEXT
// RUN: "%t.exe"
// REGIONS: coordinate regions: 2
// CONTEXT: context trap: 2
// clang-format on
#include "../../sdk/a2mba.h"
#include <stdint.h>

#define EXPR(x, y, z, t) ((((x) ^ (t)) + (y)) + ((z) & (t))) ^ (((x) + (z)) | (y))

A2MBA_PROTECT_NOINLINE uint32_t mix32(uint32_t x, uint32_t y, uint32_t z, uint32_t t) {
  return EXPR(x, y, z, t);
}
A2MBA_PROTECT_NOINLINE uint64_t mix64(uint64_t x, uint64_t y, uint64_t z, uint64_t t) {
  return EXPR(x, y, z, t);
}

static int check(uint64_t x, uint64_t y, uint64_t z, uint64_t t) {
  uint32_t a = (uint32_t)x, b = (uint32_t)y, c = (uint32_t)z, d = (uint32_t)t;
  return mix32(a, b, c, d) == (EXPR(a, b, c, d)) && mix64(x, y, z, t) == (EXPR(x, y, z, t));
}

int main(void) {
  uint64_t s = 7;
  const uint64_t edge[] = {0, 1, UINT32_MAX, UINT64_MAX, UINT64_C(1) << 31, UINT64_C(1) << 63};
  for (unsigned i = 0; i < 6; ++i)
    for (unsigned j = 0; j < 6; ++j)
      if (!check(edge[i], edge[j], ~edge[i], ~edge[j]))
        return 1;
  for (unsigned i = 0; i < 2048; ++i) {
    uint64_t v[4];
    for (unsigned j = 0; j < 4; ++j) {
      s = s * UINT64_C(6364136223846793005) + 1;
      v[j] = s;
    }
    if (!check(v[0], v[1], v[2], v[3]))
      return 1;
  }
  return 0;
}
