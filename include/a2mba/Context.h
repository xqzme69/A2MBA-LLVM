#pragma once

#include "a2mba/Config.h"
#include "a2mba/Random.h"
#include "a2mba/Statistics.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <utility>

namespace a2mba {

class A2MBAContext {
public:
  explicit A2MBAContext(Config configuration);

  llvm::Expected<std::pair<llvm::APInt, llvm::APInt>> nextModularPair(unsigned bitWidth);

  Config config;
  RandomSource random;
  Statistics statistics;

private:
  llvm::DenseSet<std::uint64_t> usedConstants;
};

} // namespace a2mba
