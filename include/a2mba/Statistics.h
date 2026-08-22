#pragma once

#include "a2mba/Eligibility.h"

#include <array>
#include <cstdint>

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace a2mba {

struct Statistics {
  std::uint64_t functionsVisited = 0;
  std::uint64_t functionsSelected = 0;
  std::uint64_t functionsTransformed = 0;
  std::uint64_t instructionsVisited = 0;
  std::uint64_t candidates = 0;
  std::uint64_t instructionsTransformed = 0;
  std::uint64_t ruleExplosions = 0;
  std::uint64_t modularScales = 0;
  std::uint64_t contextTraps = 0;
  std::uint64_t adcTransforms = 0;
  std::uint64_t sbbTransforms = 0;
  std::uint64_t paperRotates = 0;

  void recordSkip(SkipReason reason);
  void print(llvm::raw_ostream &output) const;

private:
  std::array<std::uint64_t, static_cast<unsigned>(SkipReason::Count)> skipped{};
};

} // namespace a2mba
