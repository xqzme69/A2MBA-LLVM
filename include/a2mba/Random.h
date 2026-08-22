#pragma once

#include "llvm/Support/Error.h"

#include <cstdint>
#include <optional>

namespace a2mba {

class RandomSource {
public:
  static RandomSource cryptographic();
  static RandomSource deterministic(std::uint64_t seed);

  llvm::Expected<std::uint64_t> next64();
  llvm::Expected<std::uint64_t> uniform(std::uint64_t upperExclusive);
  llvm::Expected<bool> chance(unsigned percentage);

  bool isDeterministic() const { return deterministicState.has_value(); }

private:
  explicit RandomSource(std::optional<std::uint64_t> state) : deterministicState(state) {}

  std::optional<std::uint64_t> deterministicState;
};

} // namespace a2mba
