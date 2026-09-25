#include "Hybrid.h"

#include "a2mba/AAMBA.h"
#include "a2mba/Eligibility.h"
#include "a2mba/Metadata.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Errc.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <string>
#include <utility>

namespace a2mba::hybrid {
namespace {

llvm::Error makeError(const std::string &text) {
  return llvm::createStringError(llvm::errc::invalid_argument, "A2MBA hybrid: %s", text.c_str());
}

llvm::Value *mark(llvm::Value *value) {
  if (auto *instruction = llvm::dyn_cast<llvm::Instruction>(value)) {
    markGenerated(*instruction);
  }
  return value;
}

std::optional<hybrid_core::Op> mapOpcode(unsigned opcode) {
  using Operation = hybrid_core::Op;
  switch (opcode) {
  case llvm::Instruction::Add:
    return Operation::Add;
  case llvm::Instruction::Sub:
    return Operation::Sub;
  case llvm::Instruction::Mul:
    return Operation::Mul;
  case llvm::Instruction::And:
    return Operation::And;
  case llvm::Instruction::Or:
    return Operation::Or;
  case llvm::Instruction::Xor:
    return Operation::Xor;
  default:
    return {};
  }
}

bool supportsNativeTarget(const llvm::Module &module) {
  const llvm::Triple triple(module.getTargetTriple());
  return triple.getArch() == llvm::Triple::x86_64 && (triple.isOSLinux() || triple.isOSWindows());
}

bool validContextParameters(const ContextTrapParameters &parameters, unsigned bitWidth) {
  return parameters.shift && parameters.shift < bitWidth - 1 && parameters.selectedBits &&
         !(parameters.selectedBits >> parameters.shift) &&
         static_cast<unsigned>(parameters.variant) <
             static_cast<unsigned>(ContextTrapVariant::Count) &&
         static_cast<unsigned>(parameters.envelope.variant) <
             static_cast<unsigned>(NonlinearEnvelopeVariant::Count);
}

llvm::Error validateEmission(llvm::IRBuilderBase &builder, llvm::BinaryOperator &operation,
                             const Plan &plan, const Layers &layers) {
  const auto eligible = checkCandidate(operation);
  if (!eligible) {
    return makeError(describe(eligible.reason));
  }

  const unsigned bitWidth = operation.getType()->getIntegerBitWidth();
  if (operation.getOpcode() != plan.originalOpcode ||
      bitWidth != static_cast<unsigned>(plan.expression.width)) {
    return makeError("plan does not match original operation");
  }
  if (builder.GetInsertBlock() != operation.getParent() ||
      builder.GetInsertPoint() != operation.getIterator()) {
    return makeError("builder must insert immediately before the original operation");
  }
  if (auto reason = hybrid_core::validate(plan.expression); !reason.empty()) {
    return makeError(reason);
  }
  if (plan.native && (plan.native->width != plan.expression.width ||
                      !supportsNativeTarget(*operation.getModule()))) {
    return makeError("native plan does not match the expression width or target");
  }
  if (layers.context && !validContextParameters(*layers.context, bitWidth)) {
    return makeError("invalid ContextTrap parameters");
  }
  if (layers.contextCuts.size() > 4 || (plan.native && !layers.contextCuts.empty())) {
    return makeError("internal ContextTrap cuts require PureIR mode and at most four cuts");
  }

  std::vector<hybrid_core::Id> cutIds;
  for (const auto &cut : layers.contextCuts) {
    if (cut.node >= plan.expression.nodes.size() ||
        !hybrid_core::arity(plan.expression.nodes[cut.node].op) ||
        !validContextParameters(cut.parameters, bitWidth) ||
        std::find(cutIds.begin(), cutIds.end(), cut.node) != cutIds.end()) {
      return makeError("invalid/duplicate internal ContextTrap cut");
    }
    cutIds.push_back(cut.node);
  }
  if (layers.architectural) {
    const TransformKind kind = layers.architectural->kind;
    if ((kind != TransformKind::Adc && kind != TransformKind::Sbb) ||
        !supportsNativeTarget(*operation.getModule())) {
      return makeError("unsupported architectural layer");
    }
  }
  if (static_cast<unsigned>(layers.nonlinear.variant) >=
      static_cast<unsigned>(NonlinearEnvelopeVariant::Count)) {
    return makeError("invalid nonlinear envelope variant");
  }
  return llvm::Error::success();
}

llvm::Expected<llvm::Value *> emitNative(llvm::IRBuilderBase &builder,
                                         llvm::BinaryOperator &operation, const Plan &plan,
                                         llvm::Value *leftOperand, llvm::Value *rightOperand) {
  auto rendered = hybrid_core::renderLLVMAssembly(*plan.native);
  if (!rendered) {
    return makeError(rendered.error);
  }

  llvm::Type *valueType = operation.getType();
  llvm::Type *returnType = valueType;
  if (plan.native->scratchRegisters > 1) {
    llvm::SmallVector<llvm::Type *, 6> fields(plan.native->scratchRegisters, valueType);
    returnType = llvm::StructType::get(operation.getContext(), fields);
  }
  auto *functionType = llvm::FunctionType::get(returnType, {valueType, valueType}, false);
  if (auto error = llvm::InlineAsm::verify(functionType, rendered.value->constraints)) {
    return std::move(error);
  }
  auto *assembly = llvm::InlineAsm::get(
      functionType, rendered.value->text, rendered.value->constraints,
      /*hasSideEffects=*/false, /*isAlignStack=*/false, llvm::InlineAsm::AD_ATT,
      /*canThrow=*/false);
  llvm::Value *replacement = mark(builder.CreateCall(
      functionType, assembly, {leftOperand, rightOperand}, "a2mba.hybrid.native"));
  if (plan.native->scratchRegisters > 1) {
    replacement = mark(builder.CreateExtractValue(replacement, {plan.native->resultRegister},
                                                  "a2mba.hybrid.result"));
  }
  return replacement;
}

llvm::Value *emitIR(llvm::IRBuilderBase &builder, llvm::BinaryOperator &operation, const Plan &plan,
                    const Layers &layers, llvm::Value *leftOperand, llvm::Value *rightOperand) {
  llvm::SmallVector<llvm::Value *, 64> values;
  using Operation = hybrid_core::Op;
  for (const auto &node : plan.expression.nodes) {
    llvm::Value *result = nullptr;
    auto *left = hybrid_core::arity(node.op) ? values[node.left] : nullptr;
    auto *right = hybrid_core::arity(node.op) == 2 ? values[node.right] : nullptr;
    switch (node.op) {
    case Operation::Input:
      result = node.payload ? rightOperand : leftOperand;
      break;
    case Operation::Constant:
      result = llvm::ConstantInt::get(operation.getType(), node.payload);
      break;
    case Operation::Add:
      result = builder.CreateAdd(left, right);
      break;
    case Operation::Sub:
      result = builder.CreateSub(left, right);
      break;
    case Operation::Mul:
      result = builder.CreateMul(left, right);
      break;
    case Operation::And:
      result = builder.CreateAnd(left, right);
      break;
    case Operation::Or:
      result = builder.CreateOr(left, right);
      break;
    case Operation::Xor:
      result = builder.CreateXor(left, right);
      break;
    case Operation::Not:
      result = builder.CreateNot(left);
      break;
    case Operation::Neg:
      result = builder.CreateNeg(left);
      break;
    }
    result = mark(result);
    const auto nodeIndex = static_cast<hybrid_core::Id>(values.size());
    for (const auto &cut : layers.contextCuts) {
      if (cut.node == nodeIndex) {
        result = applyContextTrap(builder, *result, cut.parameters);
      }
    }
    values.push_back(result);
  }
  return values[plan.expression.root];
}

llvm::Expected<llvm::Value *> applyOuterLayers(llvm::IRBuilderBase &builder,
                                               llvm::BinaryOperator &operation,
                                               llvm::Value *replacement, const Layers &layers) {
  if (layers.context) {
    replacement = applyContextTrap(builder, *replacement, *layers.context);
  }
  if (!layers.architectural) {
    return replacement;
  }

  const auto &layer = *layers.architectural;
  auto wrapped = applyArchitecturalIdentity(
      builder, *replacement, llvm::APInt(operation.getType()->getIntegerBitWidth(), layer.constant),
      layer.kind);
  if (!wrapped) {
    return wrapped.takeError();
  }
  return *wrapped;
}

} // namespace

llvm::Expected<Plan> plan(llvm::BinaryOperator &operation, const Options &options) {
  const auto eligible = checkCandidate(operation);
  if (!eligible) {
    return makeError(describe(eligible.reason));
  }
  auto opcode = mapOpcode(operation.getOpcode());
  if (!opcode) {
    return makeError("unsupported opcode");
  }
  auto seed = hybrid_core::makeSeed(
      *opcode, static_cast<hybrid_core::Width>(operation.getType()->getIntegerBitWidth()));
  if (!seed) {
    return makeError(seed.error);
  }
  if (options.emission == EmissionMode::NativeRegisters) {
    if (!supportsNativeTarget(*operation.getModule())) {
      return makeError("native emitter supports only x86-64 Linux/Windows");
    }
    auto selected = hybrid_core::prepareNative(*seed.value, options.search, options.native);
    if (!selected) {
      return makeError(selected.error);
    }
    return Plan{static_cast<unsigned>(operation.getOpcode()),
                std::move(selected.value->candidate.program), std::move(selected.value->native),
                std::move(selected.value->report)};
  }
  auto selected = hybrid_core::search(*seed.value, options.search);
  if (!selected) {
    return makeError(selected.error);
  }
  return Plan{static_cast<unsigned>(operation.getOpcode()),
              std::move(selected.value->candidates.front().program), std::nullopt,
              std::move(selected.value->report)};
}

llvm::Expected<llvm::Value *> emitWithOperands(llvm::IRBuilderBase &builder,
                                               llvm::BinaryOperator &operation, const Plan &plan,
                                               llvm::Value &leftOperand, llvm::Value &rightOperand,
                                               const Layers &layers) {
  if (auto error = validateEmission(builder, operation, plan, layers)) {
    return std::move(error);
  }
  if (leftOperand.getType() != operation.getType() ||
      rightOperand.getType() != operation.getType()) {
    return makeError("replacement operands do not match the original integer type");
  }

  auto replacement = [&]() -> llvm::Expected<llvm::Value *> {
    if (plan.native) {
      return emitNative(builder, operation, plan, &leftOperand, &rightOperand);
    }
    return emitIR(builder, operation, plan, layers, &leftOperand, &rightOperand);
  }();
  if (!replacement) {
    return replacement.takeError();
  }
  llvm::Value *hardened =
      applyNonlinearEnvelope(builder, **replacement, leftOperand, rightOperand, layers.nonlinear);
  return applyOuterLayers(builder, operation, hardened, layers);
}

llvm::Expected<llvm::Value *> emit(llvm::IRBuilderBase &builder, llvm::BinaryOperator &operation,
                                   const Plan &plan, const Layers &layers) {
  return emitWithOperands(builder, operation, plan, *operation.getOperand(0),
                          *operation.getOperand(1), layers);
}
} // namespace a2mba::hybrid
