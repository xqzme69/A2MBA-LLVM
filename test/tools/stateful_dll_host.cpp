#include "stateful_dll.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <iostream>
#include <limits>
#include <thread>

namespace {

template <typename Word> Word reference_mix(const std::array<Word, 8> &operands) {
  Word value = operands[0] + operands[1];
  value ^= operands[2];
  value -= operands[3];
  value |= operands[4];
  value &= operands[5];
  value *= operands[6];
  return value ^ operands[7];
}

bool check_vector(decltype(&protected_mix32) mix32, decltype(&protected_mix64) mix64,
                  const std::array<std::uint64_t, 8> &operands) {
  std::array<std::uint32_t, 8> narrow{};
  for (std::size_t index = 0; index < narrow.size(); ++index) {
    narrow[index] = static_cast<std::uint32_t>(operands[index]);
  }
  return mix64(operands[0], operands[1], operands[2], operands[3], operands[4], operands[5],
               operands[6], operands[7]) == reference_mix(operands) &&
         mix32(narrow[0], narrow[1], narrow[2], narrow[3], narrow[4], narrow[5], narrow[6],
               narrow[7]) == reference_mix(narrow);
}

bool check_generated(decltype(&protected_mix32) mix32, decltype(&protected_mix64) mix64,
                     std::uint64_t seed, unsigned count) {
  for (unsigned iteration = 0; iteration < count; ++iteration) {
    std::array<std::uint64_t, 8> operands{};
    for (auto &operand : operands) {
      seed = seed * UINT64_C(6364136223846793005) + 1;
      operand = seed;
    }
    if (!check_vector(mix32, mix64, operands)) {
      return false;
    }
  }
  return true;
}

struct CallbackFailure {
  std::uint64_t value;
};

std::uint64_t callback_value = 0;

void capture_callback(std::uint64_t value) { callback_value = value; }
void throwing_callback(std::uint64_t value) { throw CallbackFailure{value}; }

bool check_library(HMODULE library) {
  const auto mix32 =
      reinterpret_cast<decltype(&protected_mix32)>(GetProcAddress(library, "protected_mix32"));
  const auto mix64 =
      reinterpret_cast<decltype(&protected_mix64)>(GetProcAddress(library, "protected_mix64"));
  const auto invoke =
      reinterpret_cast<decltype(&invoke_mix)>(GetProcAddress(library, "invoke_mix"));
  if (!mix32 || !mix64 || !invoke) {
    std::cerr << "missing DLL export\n";
    return false;
  }

  const std::array<std::uint64_t, 8> edges{0,
                                           1,
                                           std::numeric_limits<std::uint64_t>::max(),
                                           UINT64_C(0x8000000000000000),
                                           UINT64_C(0x7fffffffffffffff),
                                           UINT64_C(0xffffffff),
                                           UINT64_C(0x80000000),
                                           UINT64_C(0xaaaaaaaaaaaaaaaa)};
  for (unsigned offset = 0; offset < edges.size(); ++offset) {
    auto operands = edges;
    for (unsigned index = 0; index < operands.size(); ++index) {
      operands[index] = edges[(index + offset) % edges.size()];
    }
    if (!check_vector(mix32, mix64, operands)) {
      std::cerr << "edge vector mismatch\n";
      return false;
    }
  }
  if (!check_generated(mix32, mix64, UINT64_C(0x6a09e667f3bcc909), 4096)) {
    std::cerr << "generated vector mismatch\n";
    return false;
  }

  std::atomic<bool> matched{true};
  std::array<std::thread, 4> threads;
  for (unsigned index = 0; index < threads.size(); ++index) {
    threads[index] = std::thread([=, &matched] {
      if (!check_generated(mix32, mix64, UINT64_C(0xbb67ae8584caa73b) + index, 1024)) {
        matched.store(false, std::memory_order_relaxed);
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }
  if (!matched.load(std::memory_order_relaxed)) {
    std::cerr << "concurrent vector mismatch\n";
    return false;
  }

  std::uint32_t destructions = 0;
  const std::array<std::uint64_t, 8> callback_operands{UINT64_C(0x123456789abcdef0),
                                                       UINT64_C(0xfedcba9876543210),
                                                       UINT64_C(0x8000000000000000),
                                                       9,
                                                       0,
                                                       std::numeric_limits<std::uint64_t>::max(),
                                                       3,
                                                       1};
  auto adjusted_operands = callback_operands;
  adjusted_operands[0] =
      ((callback_operands[0] + callback_operands[1]) ^ UINT64_C(0x9e3779b97f4a7c15)) -
      callback_operands[1];
  const auto expected = reference_mix(adjusted_operands);
  invoke(capture_callback, callback_operands.data(), callback_operands[0], callback_operands[1],
         &destructions);
  if (callback_value != expected || destructions != 1) {
    std::cerr << "callback or normal cleanup mismatch\n";
    return false;
  }
  bool caught = false;
  try {
    invoke(throwing_callback, callback_operands.data(), callback_operands[0], callback_operands[1],
           &destructions);
  } catch (const CallbackFailure &error) {
    caught = error.value == expected;
  }
  if (!caught || destructions != 2) {
    std::cerr << "exception or unwind cleanup mismatch\n";
    return false;
  }
  callback_value = 0;
  invoke(capture_callback, callback_operands.data(), callback_operands[0], callback_operands[1],
         &destructions);
  if (callback_value != expected || destructions != 3) {
    std::cerr << "callback after exception mismatch\n";
    return false;
  }
  return true;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc != 2) {
    return 2;
  }
  HMODULE library = LoadLibraryW(argv[1]);
  if (!library) {
    std::cerr << "LoadLibrary failed: " << GetLastError() << '\n';
    return 1;
  }
  const bool matched = check_library(library);
  if (!FreeLibrary(library)) {
    std::cerr << "FreeLibrary failed: " << GetLastError() << '\n';
    return 1;
  }
  if (!matched) {
    return 1;
  }
  std::cout << "PASS: 16400 integer comparisons, callbacks, and C++ exception cleanup\n";
  return 0;
}
