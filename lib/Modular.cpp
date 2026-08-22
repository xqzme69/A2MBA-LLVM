#include "a2mba/Modular.h"

#include "llvm/Support/Errc.h"

namespace a2mba {

llvm::Expected<llvm::APInt> modularInverseOdd(const llvm::APInt &value) {
  if (!value[0]) {
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "modular inverse requires an odd value");
  }

  const unsigned bitWidth = value.getBitWidth();
  llvm::APInt inverse(bitWidth, 1);

  // Newton iteration doubles the number of correct low bits each round.
  for (unsigned correctBits = 1; correctBits < bitWidth; correctBits *= 2) {
    inverse *= llvm::APInt(bitWidth, 2) - value * inverse;
  }

  if (!isModularInverse(value, inverse)) {
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "failed to compute modular inverse");
  }

  return inverse;
}

bool isModularInverse(const llvm::APInt &value, const llvm::APInt &candidateInverse) {
  return value.getBitWidth() == candidateInverse.getBitWidth() &&
         value * candidateInverse == llvm::APInt(value.getBitWidth(), 1);
}

} // namespace a2mba
