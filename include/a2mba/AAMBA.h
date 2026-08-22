#pragma once

#include "a2mba/Config.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/Error.h"

namespace llvm {
class IRBuilderBase;
class Value;
} // namespace llvm

namespace a2mba {

llvm::Expected<llvm::Value *> applyArchitecturalIdentity(llvm::IRBuilderBase &builder,
                                                         llvm::Value &input,
                                                         const llvm::APInt &constant,
                                                         TransformKind transform);

} // namespace a2mba
