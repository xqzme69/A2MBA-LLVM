#pragma once

namespace llvm {
class BinaryOperator;
class Instruction;
} // namespace llvm

namespace a2mba {

enum class SkipReason {
  None,
  Generated,
  NoUses,
  UnsupportedInstruction,
  UnsupportedType,
  PoisonGeneratingFlags,
  UnsupportedTransform,
  Probability,
  Count,
};

struct EligibilityResult {
  llvm::BinaryOperator *operation = nullptr;
  unsigned bitWidth = 0;
  SkipReason reason = SkipReason::UnsupportedInstruction;

  explicit operator bool() const { return operation != nullptr; }
};

EligibilityResult checkCandidate(llvm::Instruction &instruction);
const char *describe(SkipReason reason);

} // namespace a2mba
