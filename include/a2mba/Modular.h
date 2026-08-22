#pragma once

#include "llvm/ADT/APInt.h"
#include "llvm/Support/Error.h"

namespace a2mba {

llvm::Expected<llvm::APInt> modularInverseOdd(const llvm::APInt &value);
bool isModularInverse(const llvm::APInt &value, const llvm::APInt &candidateInverse);

} // namespace a2mba
