#include "a2mba/Eligibility.h"

#include "a2mba/Metadata.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"

namespace a2mba {

EligibilityResult checkCandidate(llvm::Instruction &instruction) {
  if (isGenerated(instruction)) {
    return {nullptr, 0, SkipReason::Generated};
  }
  if (instruction.use_empty()) {
    return {nullptr, 0, SkipReason::NoUses};
  }

  auto *operation = llvm::dyn_cast<llvm::BinaryOperator>(&instruction);
  if (!operation) {
    return {nullptr, 0, SkipReason::UnsupportedInstruction};
  }

  if (!operation->getType()->isIntegerTy(32) && !operation->getType()->isIntegerTy(64)) {
    return {nullptr, 0, SkipReason::UnsupportedType};
  }

  switch (operation->getOpcode()) {
  case llvm::Instruction::Add:
  case llvm::Instruction::Sub:
  case llvm::Instruction::Mul:
  case llvm::Instruction::And:
  case llvm::Instruction::Or:
  case llvm::Instruction::Xor:
    break;
  default:
    return {nullptr, 0, SkipReason::UnsupportedInstruction};
  }

  if (const auto *overflowing = llvm::dyn_cast<llvm::OverflowingBinaryOperator>(operation);
      overflowing && (overflowing->hasNoSignedWrap() || overflowing->hasNoUnsignedWrap())) {
    return {nullptr, 0, SkipReason::PoisonGeneratingFlags};
  }

  if (operation->getOpcode() == llvm::Instruction::Or &&
      llvm::cast<llvm::PossiblyDisjointInst>(operation)->isDisjoint()) {
    return {nullptr, 0, SkipReason::PoisonGeneratingFlags};
  }

  return {operation, operation->getType()->getIntegerBitWidth(), SkipReason::None};
}

const char *describe(SkipReason reason) {
  switch (reason) {
  case SkipReason::None:
    return "none";
  case SkipReason::Generated:
    return "generated instruction";
  case SkipReason::NoUses:
    return "unused result";
  case SkipReason::UnsupportedInstruction:
    return "unsupported instruction";
  case SkipReason::UnsupportedType:
    return "unsupported type";
  case SkipReason::PoisonGeneratingFlags:
    return "poison-generating flags";
  case SkipReason::UnsupportedTransform:
    return "unsupported transform";
  case SkipReason::Probability:
    return "probability filter";
  case SkipReason::Count:
    return "invalid reason";
  }
  return "unknown";
}

} // namespace a2mba
