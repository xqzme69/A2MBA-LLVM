#include "stateful_dll.h"
#include "a2mba.h"

extern "C" __declspec(dllexport) A2MBA_PROTECT_NOINLINE std::uint32_t
protected_mix32(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t e,
                std::uint32_t f, std::uint32_t g, std::uint32_t h) {
  return ((((((a + b) ^ c) - d) | e) & f) * g) ^ h;
}

extern "C" __declspec(dllexport) A2MBA_PROTECT_NOINLINE std::uint64_t
protected_mix64(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e,
                std::uint64_t f, std::uint64_t g, std::uint64_t h) {
  return ((((((a + b) ^ c) - d) | e) & f) * g) ^ h;
}
