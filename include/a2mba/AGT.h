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

enum class NonlinearEnvelopeVariant : std::uint8_t {
  XorAdd,
  AddXor,
  SubXor,
  Count,
};

struct NonlinearEnvelopeParameters {
  std::uint64_t firstSalt = 0x9e3779b97f4a7c15ULL;
  std::uint64_t secondSalt = 0xd1b54a32d192ed03ULL;
  NonlinearEnvelopeVariant variant = NonlinearEnvelopeVariant::XorAdd;
};

struct ContextTrapParameters {
  unsigned shift;
  std::uint64_t selectedBits;
  ContextTrapVariant variant;
  NonlinearEnvelopeParameters envelope;
};

llvm::Value *applyRuleExplosion(llvm::IRBuilderBase &builder, llvm::BinaryOperator &operation,
                                const llvm::APInt &constant, const llvm::APInt &inverse);

llvm::Value *applyModularScale(llvm::IRBuilderBase &builder, llvm::Value &input,
                               const llvm::APInt &constant, const llvm::APInt &inverse);

llvm::Value *applyNonlinearEnvelope(llvm::IRBuilderBase &builder, llvm::Value &input,
                                    llvm::Value &leftContext, llvm::Value &rightContext,
                                    const NonlinearEnvelopeParameters &parameters);

llvm::Value *createOddModularInverse(llvm::IRBuilderBase &builder, llvm::Value &key,
                                     const NonlinearEnvelopeParameters &parameters);

llvm::Value *applyContextTrap(llvm::IRBuilderBase &builder, llvm::Value &input,
                              const ContextTrapParameters &parameters);

} // namespace a2mba
