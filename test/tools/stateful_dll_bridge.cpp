#include "a2mba.h"
#include "stateful_dll.h"

namespace {

struct CallGuard {
  std::uint32_t *destructions;

  ~CallGuard() { ++*destructions; }
};

} // namespace

extern "C" __declspec(dllexport) A2MBA_PROTECT_NOINLINE void
invoke_mix(MixCallback callback, const std::uint64_t *operands, std::uint64_t x, std::uint64_t y,
           std::uint32_t *destructions) {
  CallGuard guard{destructions};
  const auto adjusted = ((x + y) ^ UINT64_C(0x9e3779b97f4a7c15)) - y;
  const auto result = protected_mix64(adjusted, operands[1], operands[2], operands[3], operands[4],
                                      operands[5], operands[6], operands[7]);
  callback(result);
}
