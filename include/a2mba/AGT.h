#pragma once

#include "llvm/ADT/APInt.h"

#include <cstdint>

namespace llvm {
class BinaryOperator;
class IRBuilderBase;
class Value;
} // namespace llvm

namespace a2mba {

enum class ContextTrapVariant : std::uint8_t {
  AddSubOr,
  SubAddXor,
  XorChainAdd,
  OrXor,
  TriggerCancelOr,
  TrapDeltaAdd,
  Count,
};

struct ContextTrapParameters {
  unsigned shift;
  std::uint64_t selectedBits;
  ContextTrapVariant variant;
};

llvm::Value *applyRuleExplosion(llvm::IRBuilderBase &builder, llvm::BinaryOperator &operation,
                                const llvm::APInt &constant, const llvm::APInt &inverse);

llvm::Value *applyModularScale(llvm::IRBuilderBase &builder, llvm::Value &input,
                               const llvm::APInt &constant, const llvm::APInt &inverse);

llvm::Value *applyContextTrap(llvm::IRBuilderBase &builder, llvm::Value &input,
                              const ContextTrapParameters &parameters);

} // namespace a2mba
