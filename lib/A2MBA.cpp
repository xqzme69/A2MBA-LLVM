#include "a2mba/A2MBA.h"

#include "a2mba/AAMBA.h"
#include "a2mba/AGT.h"
#include "a2mba/Config.h"
#include "a2mba/Context.h"
#include "a2mba/Eligibility.h"
#include "a2mba/Metadata.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace a2mba {
namespace {

struct TransformPlan {
  llvm::BinaryOperator *operation;
  TransformKind transform;
  unsigned depth;
  std::optional<ContextTrapParameters> contextTrap;
};

template <typename T> T takeOrFatal(llvm::Expected<T> value) {
  if (!value) {
    llvm::report_fatal_error(llvm::Twine("A2MBA: ") + llvm::toString(value.takeError()), false);
  }
  return std::move(*value);
}

bool isSupportedTarget(const llvm::Module &module) {
  const llvm::Triple triple(module.getTargetTriple());
  return triple.getArch() == llvm::Triple::x86_64 && (triple.isOSLinux() || triple.isOSWindows());
}

bool containsUserInlineAssembly(llvm::Function &function) {
  for (llvm::Instruction &instruction : llvm::instructions(function)) {
    const auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction);
    if (call && call->isInlineAsm()) {
      return true;
    }
  }
  return false;
}

bool isSelected(const llvm::Function &function, const FunctionAnnotations &annotations,
                const Config &config, const llvm::Regex *functionPattern) {
  if (annotations.ignoredFunctions.contains(&function)) {
    return false;
  }

  switch (config.functionSelection) {
  case FunctionSelectionKind::Annotated:
    return annotations.protectedFunctions.contains(&function);
  case FunctionSelectionKind::All:
    return true;
  case FunctionSelectionKind::Regex:
    return functionPattern && functionPattern->match(function.getName());
  }
  return false;
}

bool supportsRuleExplosion(const llvm::BinaryOperator &operation) {
  return operation.getOpcode() == llvm::Instruction::Add ||
         operation.getOpcode() == llvm::Instruction::Sub ||
         operation.getOpcode() == llvm::Instruction::Mul;
}

TransformKind chooseArchitecturalTransform(const Config &config, RandomSource &random) {
  if (config.mode == ImplementationMode::Paper && takeOrFatal(random.chance(15))) {
    return TransformKind::PaperRcrRcl;
  }
  return takeOrFatal(random.chance(50)) ? TransformKind::Adc : TransformKind::Sbb;
}

TransformKind chooseAutomaticTransform(llvm::BinaryOperator &operation,
                                       const ProtectionProfile &profile, const Config &config,
                                       RandomSource &random) {
  if (takeOrFatal(random.chance(profile.architecturalProbability))) {
    return chooseArchitecturalTransform(config, random);
  }

  if (takeOrFatal(random.chance(profile.contextTrapProbability))) {
    return TransformKind::ContextTrap;
  }

  if (supportsRuleExplosion(operation)) {
    return TransformKind::RuleExplosion;
  }

  // The paper does not define modular-scaling rules for Boolean operators.
  // Preserve them by wrapping their result in a verified architectural identity.
  return chooseArchitecturalTransform(config, random);
}

unsigned chooseDepth(const ProtectionProfile &profile, RandomSource &random) {
  if (profile.minimumDepth == profile.maximumDepth) {
    return profile.minimumDepth;
  }
  const std::uint64_t span = profile.maximumDepth - profile.minimumDepth + 1;
  return profile.minimumDepth + static_cast<unsigned>(takeOrFatal(random.uniform(span)));
}

ContextTrapParameters chooseContextTrapParameters(unsigned bitWidth, RandomSource &random) {
  constexpr unsigned maximumShift = 8;
  const unsigned shiftLimit = std::min(maximumShift, bitWidth - 2);
  const unsigned shift = 1 + static_cast<unsigned>(takeOrFatal(random.uniform(shiftLimit)));
  const std::uint64_t maskLimit = (std::uint64_t{1} << shift) - 1;
  const std::uint64_t selectedBits = 1 + takeOrFatal(random.uniform(maskLimit));
  const auto variant = static_cast<ContextTrapVariant>(
      takeOrFatal(random.uniform(static_cast<std::uint64_t>(ContextTrapVariant::Count))));
  return {shift, selectedBits, variant};
}

void diagnoseSkip(const Config &config, const llvm::Function &function,
                  const llvm::Instruction &instruction, SkipReason reason) {
  if (!config.printDiagnostics || reason == SkipReason::Probability) {
    return;
  }

  llvm::errs() << "A2MBA-I201: " << function.getName() << "(): ";
  instruction.printAsOperand(llvm::errs(), false);
  llvm::errs() << " was not transformed: " << describe(reason) << '\n';
}

std::optional<TransformPlan> planTransform(llvm::BinaryOperator &operation, A2MBAContext &context) {
  const ProtectionProfile profile = context.config.profile();
  if (!takeOrFatal(context.random.chance(profile.candidateProbability))) {
    context.statistics.recordSkip(SkipReason::Probability);
    return std::nullopt;
  }

  TransformKind transform = context.config.forcedTransform;
  if (transform == TransformKind::Auto) {
    transform = chooseAutomaticTransform(operation, profile, context.config, context.random);
  }

  if (transform == TransformKind::RuleExplosion && !supportsRuleExplosion(operation)) {
    context.statistics.recordSkip(SkipReason::UnsupportedTransform);
    diagnoseSkip(context.config, *operation.getFunction(), operation,
                 SkipReason::UnsupportedTransform);
    return std::nullopt;
  }

  const unsigned depth = chooseDepth(profile, context.random);
  std::optional<ContextTrapParameters> contextTrap;
  if (transform == TransformKind::ContextTrap) {
    contextTrap =
        chooseContextTrapParameters(operation.getType()->getIntegerBitWidth(), context.random);
  }
  return TransformPlan{&operation, transform, depth, contextTrap};
}

llvm::Value *mark(llvm::Value *value) {
  if (auto *instruction = llvm::dyn_cast<llvm::Instruction>(value)) {
    markGenerated(*instruction);
  }
  return value;
}

llvm::Value *recreateOperation(llvm::IRBuilderBase &builder, llvm::BinaryOperator &operation,
                               llvm::Value *left, llvm::Value *right) {
  switch (operation.getOpcode()) {
  case llvm::Instruction::Add:
    return mark(builder.CreateAdd(left, right, "a2mba.original"));
  case llvm::Instruction::Sub:
    return mark(builder.CreateSub(left, right, "a2mba.original"));
  case llvm::Instruction::Mul:
    return mark(builder.CreateMul(left, right, "a2mba.original"));
  case llvm::Instruction::And:
    return mark(builder.CreateAnd(left, right, "a2mba.original"));
  case llvm::Instruction::Or:
    return mark(builder.CreateOr(left, right, "a2mba.original"));
  case llvm::Instruction::Xor:
    return mark(builder.CreateXor(left, right, "a2mba.original"));
  default:
    llvm_unreachable("eligibility admitted an unsupported opcode");
  }
}

llvm::Value *applyArchitecturalLayer(llvm::IRBuilderBase &builder, llvm::Value &input,
                                     TransformKind transform, A2MBAContext &context) {
  llvm::APInt constant(input.getType()->getIntegerBitWidth(), 0);
  if (transform != TransformKind::PaperRcrRcl) {
    constant = takeOrFatal(context.nextModularPair(input.getType()->getIntegerBitWidth())).first;
  }
  return takeOrFatal(applyArchitecturalIdentity(builder, input, constant, transform));
}

void recordTransform(Statistics &statistics, TransformKind transform) {
  switch (transform) {
  case TransformKind::RuleExplosion:
    ++statistics.ruleExplosions;
    break;
  case TransformKind::ModularScale:
    ++statistics.modularScales;
    break;
  case TransformKind::ContextTrap:
    ++statistics.contextTraps;
    break;
  case TransformKind::Adc:
    ++statistics.adcTransforms;
    break;
  case TransformKind::Sbb:
    ++statistics.sbbTransforms;
    break;
  case TransformKind::PaperRcrRcl:
    ++statistics.paperRotates;
    break;
  case TransformKind::Auto:
    llvm_unreachable("auto must be resolved before application");
  }
}

llvm::Value *applyPrimaryTransform(llvm::IRBuilderBase &builder, const TransformPlan &plan,
                                   A2MBAContext &context) {
  llvm::BinaryOperator &operation = *plan.operation;
  llvm::Value *result = nullptr;

  switch (plan.transform) {
  case TransformKind::RuleExplosion: {
    auto [constant, inverse] =
        takeOrFatal(context.nextModularPair(operation.getType()->getIntegerBitWidth()));
    result = applyRuleExplosion(builder, operation, constant, inverse);
    break;
  }
  case TransformKind::ModularScale: {
    auto *original =
        recreateOperation(builder, operation, operation.getOperand(0), operation.getOperand(1));
    auto [constant, inverse] =
        takeOrFatal(context.nextModularPair(operation.getType()->getIntegerBitWidth()));
    result = applyModularScale(builder, *original, constant, inverse);
    break;
  }
  case TransformKind::ContextTrap: {
    auto *original =
        recreateOperation(builder, operation, operation.getOperand(0), operation.getOperand(1));
    result = applyContextTrap(builder, *original, *plan.contextTrap);
    break;
  }
  case TransformKind::Adc:
  case TransformKind::Sbb:
  case TransformKind::PaperRcrRcl: {
    auto *original =
        recreateOperation(builder, operation, operation.getOperand(0), operation.getOperand(1));
    result = applyArchitecturalLayer(builder, *original, plan.transform, context);
    break;
  }
  case TransformKind::Auto:
    llvm_unreachable("auto must be resolved before application");
  }

  recordTransform(context.statistics, plan.transform);
  return result;
}

llvm::Value *applyAdditionalLayer(llvm::IRBuilderBase &builder, llvm::Value &input,
                                  const TransformPlan &plan, A2MBAContext &context) {
  TransformKind transform = TransformKind::ModularScale;
  if (plan.transform == TransformKind::Adc || plan.transform == TransformKind::Sbb ||
      plan.transform == TransformKind::PaperRcrRcl) {
    transform = plan.transform;
  } else if (context.config.forcedTransform == TransformKind::Auto &&
             takeOrFatal(
                 context.random.chance(context.config.profile().architecturalProbability))) {
    transform = chooseArchitecturalTransform(context.config, context.random);
  }

  if (transform == TransformKind::ModularScale) {
    auto [constant, inverse] =
        takeOrFatal(context.nextModularPair(input.getType()->getIntegerBitWidth()));
    recordTransform(context.statistics, transform);
    return applyModularScale(builder, input, constant, inverse);
  }

  recordTransform(context.statistics, transform);
  return applyArchitecturalLayer(builder, input, transform, context);
}

void applyPlan(const TransformPlan &plan, A2MBAContext &context) {
  llvm::BinaryOperator &operation = *plan.operation;
  llvm::IRBuilder<> builder(&operation);
  builder.SetCurrentDebugLocation(operation.getDebugLoc());

  llvm::Value *replacement = applyPrimaryTransform(builder, plan, context);
  for (unsigned layer = 1; layer < plan.depth; ++layer) {
    replacement = applyAdditionalLayer(builder, *replacement, plan, context);
  }

  operation.replaceAllUsesWith(replacement);
  operation.eraseFromParent();
  ++context.statistics.instructionsTransformed;
}

bool transformFunction(llvm::Function &function, A2MBAContext &context) {
  llvm::SmallVector<llvm::Instruction *, 64> originalInstructions;
  for (llvm::Instruction &instruction : llvm::instructions(function)) {
    originalInstructions.push_back(&instruction);
  }

  llvm::SmallVector<TransformPlan, 32> plans;
  for (llvm::Instruction *instruction : originalInstructions) {
    ++context.statistics.instructionsVisited;
    const EligibilityResult candidate = checkCandidate(*instruction);
    if (!candidate) {
      context.statistics.recordSkip(candidate.reason);
      if (candidate.reason == SkipReason::PoisonGeneratingFlags) {
        diagnoseSkip(context.config, function, *instruction, candidate.reason);
      }
      continue;
    }

    ++context.statistics.candidates;
    if (auto plan = planTransform(*candidate.operation, context)) {
      plans.push_back(*plan);
    }
  }

  for (const TransformPlan &plan : plans) {
    applyPlan(plan, context);
  }
  if (!plans.empty()) {
    markProtected(function);
  }
  return !plans.empty();
}

} // namespace

llvm::PreservedAnalyses A2MBAPass::run(llvm::Module &module, llvm::ModuleAnalysisManager &) {
  if (isModuleProcessed(module)) {
    return llvm::PreservedAnalyses::all();
  }

  Config configuration = takeOrFatal(Config::loadFromEnvironment());
  A2MBAContext context(std::move(configuration));

  if (!isSupportedTarget(module)) {
    if (context.config.printDiagnostics) {
      llvm::errs() << "A2MBA-I001: unsupported target triple '" << module.getTargetTriple().str()
                   << "'; expected x86-64 Linux or "
                      "Windows\n";
    }
    if (context.config.printStatistics) {
      context.statistics.print(llvm::errs());
    }
    return llvm::PreservedAnalyses::all();
  }

  std::optional<llvm::Regex> functionPattern;
  if (context.config.functionSelection == FunctionSelectionKind::Regex) {
    functionPattern.emplace(context.config.functionPattern);
    std::string regexError;
    if (!functionPattern->isValid(regexError)) {
      llvm::report_fatal_error(llvm::Twine("A2MBA: invalid function regex: ") + regexError, false);
    }
  }

  const FunctionAnnotations annotations = collectFunctionAnnotations(module);
  bool moduleChanged = false;
  for (llvm::Function &function : module) {
    if (function.isDeclaration() || function.hasAvailableExternallyLinkage()) {
      continue;
    }
    ++context.statistics.functionsVisited;

    if (!isSelected(function, annotations, context.config,
                    functionPattern ? &*functionPattern : nullptr)) {
      continue;
    }
    ++context.statistics.functionsSelected;

    if (containsUserInlineAssembly(function)) {
      if (context.config.printDiagnostics) {
        llvm::errs() << "A2MBA-I202: " << function.getName()
                     << "(): skipped because it already contains inline asm\n";
      }
      continue;
    }

    if (transformFunction(function, context)) {
      ++context.statistics.functionsTransformed;
      moduleChanged = true;
    }
  }

  if (moduleChanged) {
    markModuleProcessed(module);
  }
  if (context.config.printStatistics) {
    context.statistics.print(llvm::errs());
  }

  return moduleChanged ? llvm::PreservedAnalyses::none() : llvm::PreservedAnalyses::all();
}

} // namespace a2mba
