// REQUIRES: a2mba-wrapper, clang, host-executable, x86-registered-target
// RUN: %a2mba_wrapper --level heavy --seed 911 --hybrid ir --hybrid-region stateful --hybrid-layers context-random --stats -I"%S/../../sdk" -O3 "%s" -o "%t.ir.exe" 2>&1 | %FileCheck "%s" --check-prefix=WRAPPER-IR
// RUN: "%t.ir.exe"
// RUN: %a2mba_wrapper --level heavy --seed 912 --hybrid native --hybrid-region stateful --hybrid-layers context-random --stats -I"%S/../../sdk" -O3 "%s" -o "%t.native.exe" 2>&1 | %FileCheck "%s" --check-prefix=WRAPPER-NATIVE
// RUN: "%t.native.exe"

// WRAPPER-IR: hybrid IR: 4
// WRAPPER-IR: hybrid native: 0
// WRAPPER-IR: stateful regions: 1
// WRAPPER-NATIVE: hybrid IR: 0
// WRAPPER-NATIVE: hybrid native: 4
// WRAPPER-NATIVE: stateful regions: 1

#include "a2mba.h"

#include <stdint.h>

__attribute__((noinline))
uint64_t reference_mix(uint64_t x, uint64_t y, uint64_t z) {
  return (((x + y) ^ z) * UINT64_C(0x2545f4914f6cdd1d)) - y;
}

A2MBA_PROTECT_NOINLINE
uint64_t protected_mix(uint64_t x, uint64_t y, uint64_t z) {
  return (((x + y) ^ z) * UINT64_C(0x2545f4914f6cdd1d)) - y;
}

int main(void) {
  const uint64_t edges[] = {
      0, 1, UINT64_MAX, UINT64_C(0x8000000000000000),
      UINT64_C(0x7fffffffffffffff), UINT64_C(0xaaaaaaaaaaaaaaaa),
  };
  const unsigned edge_count = sizeof(edges) / sizeof(edges[0]);
  for (unsigned i = 0; i < edge_count; ++i) {
    for (unsigned j = 0; j < edge_count; ++j) {
      for (unsigned k = 0; k < edge_count; ++k) {
        if (protected_mix(edges[i], edges[j], edges[k]) !=
            reference_mix(edges[i], edges[j], edges[k])) {
          return 1;
        }
      }
    }
  }

  uint64_t state = UINT64_C(0x6a09e667f3bcc909);
  for (unsigned i = 0; i < 1024; ++i) {
    state = state * UINT64_C(6364136223846793005) + 1;
    const uint64_t x = state;
    state = state * UINT64_C(6364136223846793005) + 1;
    const uint64_t y = state;
    state = state * UINT64_C(6364136223846793005) + 1;
    const uint64_t z = state;
    if (protected_mix(x, y, z) != reference_mix(x, y, z)) {
      return 1;
    }
  }
  return 0;
}
