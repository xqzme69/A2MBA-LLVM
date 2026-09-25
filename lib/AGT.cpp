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

llvm::Value *createOddModularInverse(llvm::IRBuilderBase &builder, llvm::Value &key,
                                     const NonlinearEnvelopeParameters &parameters) {
  auto *integerType = llvm::cast<llvm::IntegerType>(key.getType());
  const unsigned bitWidth = integerType->getBitWidth();
  constexpr unsigned geometricVariantBit = 29;
  if ((parameters.secondSalt >> geometricVariantBit) & 1U) {
    // For odd key, q = 1 - key is even and (1 - q) * product(1 + q^(2^i)) = 1 mod 2^w.
    auto *one = llvm::ConstantInt::get(integerType, 1);
    llvm::Value *error = mark(builder.CreateSub(one, &key, "a2mba.inverse.error"));
    llvm::Value *inverse = one;
    for (unsigned bits = 1; bits < bitWidth; bits *= 2) {
      auto *factor = mark(builder.CreateAdd(one, error, "a2mba.inverse.factor"));
      inverse = mark(builder.CreateMul(inverse, factor, "a2mba.inverse.product"));
      if (bits * 2 < bitWidth) {
        error = mark(builder.CreateMul(error, error, "a2mba.inverse.error.square"));
      }
    }
    return inverse;
  }

  const bool twoBitSeed = ((parameters.firstSalt >> 7U) & 1U) != 0;
  llvm::Value *inverse = &key;
  unsigned correctBits = 1;
  if (twoBitSeed) {
    inverse =
        mark(builder.CreateSub(llvm::ConstantInt::get(integerType, 2), &key, "a2mba.inverse"));
    correctBits = 2;
  }

  unsigned refinement = 0;
  for (; correctBits < bitWidth; correctBits *= 2, ++refinement) {
    const unsigned variant = static_cast<unsigned>(
        ((parameters.secondSalt >> (refinement * 2U)) ^ parameters.firstSalt) % 3U);
    if (variant == 0) {
      auto *keyProduct = mark(builder.CreateMul(&key, inverse, "a2mba.inverse.product"));
      auto *correction = mark(builder.CreateSub(llvm::ConstantInt::get(integerType, 2), keyProduct,
                                                "a2mba.inverse.correction"));
      inverse = mark(builder.CreateMul(inverse, correction, "a2mba.inverse.refined"));
      continue;
    }
    if (variant == 1) {
      auto *keyProduct = mark(builder.CreateMul(&key, inverse, "a2mba.inverse.product"));
      auto *error = mark(builder.CreateSub(llvm::ConstantInt::get(integerType, 1), keyProduct,
                                           "a2mba.inverse.error"));
      auto *delta = mark(builder.CreateMul(inverse, error, "a2mba.inverse.delta"));
      inverse = mark(builder.CreateAdd(inverse, delta, "a2mba.inverse.refined"));
      continue;
    }

    auto *doubledInverse = mark(builder.CreateAdd(inverse, inverse, "a2mba.inverse.doubled"));
    auto *inverseSquare = mark(builder.CreateMul(inverse, inverse, "a2mba.inverse.square"));
    auto *scaledSquare = mark(builder.CreateMul(&key, inverseSquare, "a2mba.inverse.scaled"));
    inverse = mark(builder.CreateSub(doubledInverse, scaledSquare, "a2mba.inverse.refined"));
  }
  return inverse;
}

llvm::Value *applyNonlinearEnvelope(llvm::IRBuilderBase &builder, llvm::Value &input,
                                    llvm::Value &leftContext, llvm::Value &rightContext,
                                    const NonlinearEnvelopeParameters &parameters) {
  auto *integerType = llvm::cast<llvm::IntegerType>(input.getType());
  assert(leftContext.getType() == integerType && rightContext.getType() == integerType);
  assert(static_cast<unsigned>(parameters.variant) <
         static_cast<unsigned>(NonlinearEnvelopeVariant::Count));

  auto *firstSalt = llvm::ConstantInt::get(integerType, parameters.firstSalt);
  auto *secondSalt = llvm::ConstantInt::get(integerType, parameters.secondSalt);
  llvm::Value *firstFactor = nullptr;
  llvm::Value *secondFactor = nullptr;

  switch (parameters.variant) {
  case NonlinearEnvelopeVariant::XorAdd: {
    auto *saltedLeft = mark(builder.CreateXor(&leftContext, firstSalt, "a2mba.nl.left"));
    firstFactor = mark(builder.CreateAdd(saltedLeft, &rightContext, "a2mba.nl.factor0"));
    auto *saltedRight = mark(builder.CreateAdd(&rightContext, secondSalt, "a2mba.nl.right"));
    secondFactor = mark(builder.CreateXor(saltedRight, &leftContext, "a2mba.nl.factor1"));
    break;
  }
  case NonlinearEnvelopeVariant::AddXor: {
    auto *saltedLeft = mark(builder.CreateAdd(&leftContext, firstSalt, "a2mba.nl.left"));
    firstFactor = mark(builder.CreateXor(saltedLeft, &rightContext, "a2mba.nl.factor0"));
    auto *saltedRight = mark(builder.CreateXor(&rightContext, secondSalt, "a2mba.nl.right"));
    secondFactor = mark(builder.CreateSub(saltedRight, &leftContext, "a2mba.nl.factor1"));
    break;
  }
  case NonlinearEnvelopeVariant::SubXor: {
    auto *saltedRight = mark(builder.CreateSub(&rightContext, firstSalt, "a2mba.nl.right"));
    firstFactor = mark(builder.CreateXor(&leftContext, saltedRight, "a2mba.nl.factor0"));
    auto *saltedLeft = mark(builder.CreateXor(&leftContext, secondSalt, "a2mba.nl.left"));
    secondFactor = mark(builder.CreateAdd(saltedLeft, &rightContext, "a2mba.nl.factor1"));
    break;
  }
  case NonlinearEnvelopeVariant::Count:
    llvm_unreachable("invalid nonlinear envelope variant");
  }

  auto *product = mark(builder.CreateMul(firstFactor, secondFactor, "a2mba.nl.product"));
  auto *doubled = mark(builder.CreateAdd(product, product, "a2mba.nl.doubled"));
  llvm::Value *key = nullptr;
  switch ((parameters.firstSalt ^ parameters.secondSalt) % 3U) {
  case 0:
    key = mark(builder.CreateOr(doubled, llvm::ConstantInt::get(integerType, 1), "a2mba.nl.key"));
    break;
  case 1:
    key = mark(builder.CreateAdd(doubled, llvm::ConstantInt::get(integerType, 1), "a2mba.nl.key"));
    break;
  case 2:
    key = mark(builder.CreateXor(doubled, llvm::ConstantInt::get(integerType, 1), "a2mba.nl.key"));
    break;
  }

  llvm::Value *inverse = createOddModularInverse(builder, *key, parameters);

  switch ((parameters.firstSalt >> 11U) % 3U) {
  case 0: {
    auto *encoded = mark(builder.CreateMul(&input, key, "a2mba.nl.encoded"));
    return mark(builder.CreateMul(encoded, inverse, "a2mba.nl.result"));
  }
  case 1: {
    auto *encoded = mark(builder.CreateMul(&input, inverse, "a2mba.nl.encoded"));
    return mark(builder.CreateMul(encoded, key, "a2mba.nl.result"));
  }
  case 2: {
    auto *unity = mark(builder.CreateMul(key, inverse, "a2mba.nl.unity"));
    return mark(builder.CreateMul(&input, unity, "a2mba.nl.result"));
  }
  }
  llvm_unreachable("invalid nonlinear decode variant");
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
  return applyNonlinearEnvelope(builder, *result, input, *triggerProjection, parameters.envelope);
}

} // namespace a2mba
