#include "a2mba/Context.h"

#include "a2mba/Modular.h"
#include "llvm/Support/Errc.h"

#include <cstdint>
#include <utility>

namespace a2mba {

A2MBAContext::A2MBAContext(Config configuration)
    : config(std::move(configuration)),
      random(config.seed ? RandomSource::deterministic(*config.seed)
                         : RandomSource::cryptographic()) {}

llvm::Expected<std::pair<llvm::APInt, llvm::APInt>>
A2MBAContext::nextModularPair(unsigned bitWidth) {
  if (bitWidth != 32 && bitWidth != 64) {
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "A2MBA constants require i32 or i64");
  }

  constexpr unsigned MaximumAttempts = 1U << 20;
  for (unsigned attempt = 0; attempt < MaximumAttempts; ++attempt) {
    auto randomValue = random.next64();
    if (!randomValue) {
      return randomValue.takeError();
    }

    llvm::APInt constant(bitWidth, *randomValue);
    constant.setBit(0);
    if (constant.isOne() || !usedConstants.insert(constant.getZExtValue()).second) {
      continue;
    }

    auto inverse = modularInverseOdd(constant);
    if (!inverse) {
      return inverse.takeError();
    }
    return std::make_pair(std::move(constant), std::move(*inverse));
  }

  return llvm::createStringError(llvm::errc::not_enough_memory,
                                 "could not generate a unique A2MBA constant after %u attempts",
                                 MaximumAttempts);
}

} // namespace a2mba
