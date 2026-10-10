#pragma once

#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Error.h"

#include <cstdint>

namespace a2mba {

struct FlagVMResult {
  llvm::Value *sum;
  llvm::Value *flag;
};

llvm::Expected<FlagVMResult> emitFlagVM(llvm::IRBuilderBase &builder, llvm::Value &state,
                                        llvm::Value &value, std::uint64_t siteSeed);

} // namespace a2mba
