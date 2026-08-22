#include "a2mba/AGT.h"

#include "a2mba/Metadata.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ErrorHandling.h"

#include <cassert>

namespace a2mba {
namespace {

llvm::Value *mark(llvm::Value *value) {
  if (auto *instruction = llvm::dyn_cast<llvm::Instruction>(value)) {
    markGenerated(*instruction);
  }
  return value;
}

llvm::ConstantInt *asConstant(llvm::Type *type, const llvm::APInt &value) {
  return llvm::cast<llvm::ConstantInt>(
      llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(type), value));
}

} // namespace

llvm::Value *applyRuleExplosion(llvm::IRBuilderBase &builder, llvm::BinaryOperator &operation,
                                const llvm::APInt &constant, const llvm::APInt &inverse) {
  llvm::Type *type = operation.getType();
  llvm::Value *constantValue = asConstant(type, constant);
  llvm::Value *inverseValue = asConstant(type, inverse);
  llvm::Value *left = operation.getOperand(0);
  llvm::Value *right = operation.getOperand(1);

  switch (operation.getOpcode()) {
  case llvm::Instruction::Add: {
    auto *scaledLeft = mark(builder.CreateMul(left, constantValue, "a2mba.re.left"));
    auto *scaledRight = mark(builder.CreateMul(right, constantValue, "a2mba.re.right"));
    auto *scaledSum = mark(builder.CreateAdd(scaledLeft, scaledRight, "a2mba.re.sum"));
    return mark(builder.CreateMul(scaledSum, inverseValue, "a2mba.re.result"));
  }
  case llvm::Instruction::Sub: {
    auto *scaledLeft = mark(builder.CreateMul(left, constantValue, "a2mba.re.left"));
    auto *scaledRight = mark(builder.CreateMul(right, constantValue, "a2mba.re.right"));
    auto *scaledDifference =
        mark(builder.CreateSub(scaledLeft, scaledRight, "a2mba.re.difference"));
    return mark(builder.CreateMul(scaledDifference, inverseValue, "a2mba.re.result"));
  }
  case llvm::Instruction::Mul: {
    auto *scaledLeft = mark(builder.CreateMul(left, constantValue, "a2mba.re.left"));
    auto *scaledProduct = mark(builder.CreateMul(scaledLeft, right, "a2mba.re.product"));
    return mark(builder.CreateMul(scaledProduct, inverseValue, "a2mba.re.result"));
  }
  default:
    return nullptr;
  }
}

llvm::Value *applyModularScale(llvm::IRBuilderBase &builder, llvm::Value &input,
                               const llvm::APInt &constant, const llvm::APInt &inverse) {
  llvm::Type *type = input.getType();
  auto *scaled = mark(builder.CreateMul(&input, asConstant(type, constant), "a2mba.scale.value"));
  return mark(builder.CreateMul(scaled, asConstant(type, inverse), "a2mba.scale.result"));
}

llvm::Value *applyContextTrap(llvm::IRBuilderBase &builder, llvm::Value &input,
                              const ContextTrapParameters &parameters) {
  auto *integerType = llvm::cast<llvm::IntegerType>(input.getType());
  const unsigned bitWidth = integerType->getBitWidth();
  const unsigned shift = parameters.shift;
  assert(shift != 0 && shift < bitWidth - 1);

  const llvm::APInt lowDomain = llvm::APInt::getLowBitsSet(bitWidth, shift);
  const llvm::APInt selectedMask(bitWidth, parameters.selectedBits);
  assert(!selectedMask.isZero() && (selectedMask & ~lowDomain).isZero());
  const llvm::APInt highMask = ~selectedMask;
  const llvm::APInt safeMask = llvm::APInt::getLowBitsSet(bitWidth, bitWidth - shift - 1);
  const llvm::APInt fillBit = llvm::APInt::getOneBitSet(bitWidth, bitWidth - shift - 1);
  const llvm::APInt poisonBit = llvm::APInt::getOneBitSet(bitWidth, bitWidth - shift);
  const llvm::APInt projectionMask = selectedMask | poisonBit;

  auto *shiftAmount = llvm::ConstantInt::get(integerType, shift);
  auto *low =
      mark(builder.CreateAnd(&input, asConstant(integerType, selectedMask), "a2mba.agt.low"));
  auto *high = mark(builder.CreateAnd(&input, asConstant(integerType, highMask), "a2mba.agt.high"));

  auto *trapInput =
      mark(builder.CreateAnd(&input, asConstant(integerType, safeMask), "a2mba.agt.trap.input"));
  auto *trapShifted = mark(builder.CreateShl(trapInput, shiftAmount, "a2mba.agt.trap.shifted"));
  auto *trapRestored =
      mark(builder.CreateAShr(trapShifted, shiftAmount, "a2mba.agt.trap.restored"));
  auto *trapProjection = mark(builder.CreateAnd(
      trapRestored, asConstant(integerType, projectionMask), "a2mba.agt.trap.projected"));

  auto *triggerInput =
      mark(builder.CreateOr(&input, asConstant(integerType, fillBit), "a2mba.agt.trigger.input"));
  auto *triggerShifted =
      mark(builder.CreateShl(triggerInput, shiftAmount, "a2mba.agt.trigger.shifted"));
  auto *triggerRestored =
      mark(builder.CreateAShr(triggerShifted, shiftAmount, "a2mba.agt.trigger.restored"));
  auto *triggerProjection = mark(builder.CreateAnd(
      triggerRestored, asConstant(integerType, projectionMask), "a2mba.agt.trigger.projected"));
  auto *triggerLow = mark(builder.CreateXor(triggerProjection, asConstant(integerType, poisonBit),
                                            "a2mba.agt.trigger.low"));

  llvm::Value *protectedLow = nullptr;
  llvm::Value *result = nullptr;
  switch (parameters.variant) {
  case ContextTrapVariant::AddSubOr: {
    auto *combined = mark(builder.CreateAdd(triggerLow, trapProjection, "a2mba.agt.combined"));
    protectedLow = mark(builder.CreateSub(combined, low, "a2mba.agt.protected"));
    result = mark(builder.CreateOr(high, protectedLow, "a2mba.agt.result"));
    break;
  }
  case ContextTrapVariant::SubAddXor: {
    auto *difference = mark(builder.CreateSub(triggerLow, trapProjection, "a2mba.agt.difference"));
    protectedLow = mark(builder.CreateAdd(difference, low, "a2mba.agt.protected"));
    result = mark(builder.CreateXor(high, protectedLow, "a2mba.agt.result"));
    break;
  }
  case ContextTrapVariant::XorChainAdd: {
    auto *paired = mark(builder.CreateXor(triggerLow, trapProjection, "a2mba.agt.paired"));
    protectedLow = mark(builder.CreateXor(paired, low, "a2mba.agt.protected"));
    result = mark(builder.CreateAdd(high, protectedLow, "a2mba.agt.result"));
    break;
  }
  case ContextTrapVariant::OrXor:
    protectedLow = mark(builder.CreateOr(triggerLow, trapProjection, "a2mba.agt.protected"));
    result = mark(builder.CreateXor(high, protectedLow, "a2mba.agt.result"));
    break;
  case ContextTrapVariant::TriggerCancelOr: {
    auto *cancel = mark(builder.CreateXor(trapProjection, low, "a2mba.agt.cancel"));
    protectedLow = mark(builder.CreateAdd(triggerLow, cancel, "a2mba.agt.protected"));
    result = mark(builder.CreateOr(high, protectedLow, "a2mba.agt.result"));
    break;
  }
  case ContextTrapVariant::TrapDeltaAdd: {
    auto *delta = mark(builder.CreateSub(triggerLow, low, "a2mba.agt.delta"));
    protectedLow = mark(builder.CreateAdd(trapProjection, delta, "a2mba.agt.protected"));
    result = mark(builder.CreateAdd(high, protectedLow, "a2mba.agt.result"));
    break;
  }
  case ContextTrapVariant::Count:
    llvm_unreachable("invalid Context Trap variant");
  }
  return result;
}

} // namespace a2mba
