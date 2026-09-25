#include "a2mba/A2MBA.h"

#include "Hybrid.h"
#include "a2mba/AAMBA.h"
#include "a2mba/AGT.h"
#include "a2mba/Config.h"
#include "a2mba/Context.h"
#include "a2mba/Eligibility.h"
#include "a2mba/Metadata.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/ValueTracking.h"
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
#include <vector>

namespace a2mba {
namespace {

struct HybridComposition {
  hybrid::Plan base;
  hybrid::Layers layers;
  HybridMode mode;
};

struct TransformPlan {
  llvm::BinaryOperator *operation;
  TransformKind transform;
  unsigned depth;
  std::optional<ContextTrapParameters> contextTrap;
  std::optional<HybridComposition> hybrid;
};

struct StatefulRegion {
  llvm::SmallVector<llvm::BinaryOperator *, 8> operations;
};

struct StatefulRegionPlan {
  StatefulRegion region;
  llvm::SmallVector<TransformPlan, 8> transforms;
};

struct EncodedValue {
  llvm::Value *source;
  llvm::Value *value;
  llvm::Value *inverse;
  llvm::Value *bias;
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

std::optional<hybrid_core::Op> mapHybridOpcode(unsigned opcode) {
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
    return std::nullopt;
  }
}

std::optional<hybrid::Plan> makeSeedHybridPlan(llvm::BinaryOperator &operation,
                                               const hybrid::Options &options) {
  const auto opcode = mapHybridOpcode(operation.getOpcode());
  if (!opcode) {
    return std::nullopt;
  }

  auto seed = hybrid_core::makeSeed(
      *opcode, static_cast<hybrid_core::Width>(operation.getType()->getIntegerBitWidth()));
  if (!seed) {
    return std::nullopt;
  }

  std::optional<hybrid_core::NativePlan> native;
  if (options.emission == hybrid::EmissionMode::NativeRegisters) {
    auto lowered = hybrid_core::lowerNative(*seed.value, options.native);
    if (!lowered) {
      return std::nullopt;
    }
    native = std::move(*lowered.value);
  }

  return hybrid::Plan{
      static_cast<unsigned>(operation.getOpcode()), std::move(*seed.value), std::move(native), {}};
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
  const NonlinearEnvelopeParameters envelope{
      takeOrFatal(random.next64()), takeOrFatal(random.next64()),
      static_cast<NonlinearEnvelopeVariant>(takeOrFatal(
          random.uniform(static_cast<std::uint64_t>(NonlinearEnvelopeVariant::Count))))};
  return {shift, selectedBits, variant, envelope};
}

NonlinearEnvelopeParameters chooseNonlinearEnvelope(RandomSource &random) {
  return {takeOrFatal(random.next64()), takeOrFatal(random.next64()),
          static_cast<NonlinearEnvelopeVariant>(takeOrFatal(
              random.uniform(static_cast<std::uint64_t>(NonlinearEnvelopeVariant::Count))))};
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

void diagnoseHybridSkip(const Config &config, const llvm::BinaryOperator &operation,
                        llvm::StringRef reason) {
  if (!config.printDiagnostics) {
    return;
  }

  llvm::errs() << "A2MBA-I203: " << operation.getFunction()->getName() << "(): ";
  operation.printAsOperand(llvm::errs(), false);
  llvm::errs() << " was not transformed: hybrid planning failed: " << reason << '\n';
}

hybrid::Options hybridOptions(const Config &config, const ProtectionProfile &profile,
                              RandomSource &random) {
  hybrid::Options options;
  options.search.seed = takeOrFatal(random.next64());

  switch (config.level) {
  case ProtectionLevel::Light:
    options.search.rounds = 2;
    options.search.maxEGraphNodes = 128;
    options.search.maxMatches = 256;
    options.search.maxMatchSteps = 50000;
    options.search.minimumAstNodes = 7;
    options.search.maxAstNodes = 31;
    options.search.beamWidth = 4;
    options.native.maxScratchRegisters = 3;
    options.native.maxInstructions = 48;
    break;
  case ProtectionLevel::Balanced:
    options.search.minimumAstNodes = 11;
    break;
  case ProtectionLevel::Medium:
    options.search.rounds = 4;
    options.search.maxEGraphNodes = 256;
    options.search.maxMatches = 768;
    options.search.maxMatchSteps = 200000;
    options.search.minimumAstNodes = 15;
    options.search.maxAstNodes = 95;
    options.search.beamWidth = 6;
    options.native.maxScratchRegisters = 6;
    options.native.maxInstructions = 160;
    break;
  case ProtectionLevel::Heavy:
    options.search.rounds = 5;
    options.search.maxEGraphNodes = 384;
    options.search.maxMatches = 1536;
    options.search.maxMatchSteps = 500000;
    options.search.minimumAstNodes = 31;
    options.search.maxAstNodes = 127;
    options.search.beamWidth = 8;
    options.native.maxScratchRegisters = 6;
    options.native.maxInstructions = 256;
    break;
  }

  unsigned searchDepth = chooseDepth(profile, random);
  searchDepth = std::clamp(searchDepth, 3U, 16U);
  options.search.maxDepth = searchDepth;
  options.emission = config.hybridMode == HybridMode::NativeRegisters
                         ? hybrid::EmissionMode::NativeRegisters
                         : hybrid::EmissionMode::PureIR;
  return options;
}

void addHybridContextLayer(hybrid::Layers &layers, const hybrid::Plan &plan, HybridMode mode,
                           unsigned bitWidth, RandomSource &random) {
  ContextTrapParameters parameters = chooseContextTrapParameters(bitWidth, random);
  if (mode == HybridMode::NativeRegisters) {
    layers.context = parameters;
    return;
  }

  std::vector<hybrid_core::Id> internalOperations;
  for (hybrid_core::Id node = 0; node < plan.expression.nodes.size(); ++node) {
    if (node != plan.expression.root && hybrid_core::arity(plan.expression.nodes[node].op) != 0) {
      internalOperations.push_back(node);
    }
  }
  if (internalOperations.empty()) {
    internalOperations.push_back(plan.expression.root);
  }
  const auto selected = takeOrFatal(random.uniform(internalOperations.size()));
  layers.contextCuts.push_back({internalOperations[selected], parameters});
}

hybrid::Layers chooseHybridLayers(llvm::BinaryOperator &operation, const hybrid::Plan &plan,
                                  A2MBAContext &context) {
  hybrid::Layers layers;
  layers.nonlinear = chooseNonlinearEnvelope(context.random);
  const ProtectionProfile profile = context.config.profile();
  const TransformKind forced = context.config.forcedTransform;

  if (context.config.hybridLayers == HybridLayerMode::None) {
    if (forced == TransformKind::ContextTrap) {
      addHybridContextLayer(layers, plan, context.config.hybridMode,
                            operation.getType()->getIntegerBitWidth(), context.random);
    } else if (forced == TransformKind::Adc || forced == TransformKind::Sbb) {
      auto [constant, inverse] =
          takeOrFatal(context.nextModularPair(operation.getType()->getIntegerBitWidth()));
      (void)inverse;
      layers.architectural = hybrid::ArchitecturalLayer{forced, constant.getZExtValue()};
    }
    return layers;
  }

  if (context.config.hybridLayers != HybridLayerMode::Profile) {
    addHybridContextLayer(layers, plan, context.config.hybridMode,
                          operation.getType()->getIntegerBitWidth(), context.random);
    TransformKind architectural = TransformKind::Adc;
    if (context.config.hybridLayers == HybridLayerMode::ContextSbb) {
      architectural = TransformKind::Sbb;
    } else if (context.config.hybridLayers == HybridLayerMode::ContextRandom) {
      architectural =
          takeOrFatal(context.random.chance(50)) ? TransformKind::Adc : TransformKind::Sbb;
    }
    auto [constant, inverse] =
        takeOrFatal(context.nextModularPair(operation.getType()->getIntegerBitWidth()));
    (void)inverse;
    layers.architectural = hybrid::ArchitecturalLayer{architectural, constant.getZExtValue()};
    return layers;
  }

  const bool useContext = forced == TransformKind::ContextTrap ||
                          (forced == TransformKind::Auto &&
                           takeOrFatal(context.random.chance(profile.contextTrapProbability)));
  if (useContext) {
    addHybridContextLayer(layers, plan, context.config.hybridMode,
                          operation.getType()->getIntegerBitWidth(), context.random);
  }

  TransformKind architectural = TransformKind::Auto;
  if (forced == TransformKind::Adc || forced == TransformKind::Sbb) {
    architectural = forced;
  } else if (forced == TransformKind::Auto &&
             takeOrFatal(context.random.chance(profile.architecturalProbability))) {
    architectural =
        takeOrFatal(context.random.chance(50)) ? TransformKind::Adc : TransformKind::Sbb;
  }
  if (architectural != TransformKind::Auto) {
    auto [constant, inverse] =
        takeOrFatal(context.nextModularPair(operation.getType()->getIntegerBitWidth()));
    (void)inverse;
    layers.architectural = hybrid::ArchitecturalLayer{architectural, constant.getZExtValue()};
  }
  return layers;
}

std::optional<TransformPlan> planHybridTransform(llvm::BinaryOperator &operation,
                                                 A2MBAContext &context,
                                                 const ProtectionProfile &profile) {
  hybrid::Options options = hybridOptions(context.config, profile, context.random);
  auto base = hybrid::plan(operation, options);
  if (!base) {
    const std::string reason = llvm::toString(base.takeError());
    context.statistics.recordSkip(SkipReason::HybridPlanningFailure);
    diagnoseHybridSkip(context.config, operation, reason);
    return std::nullopt;
  }

  hybrid::Layers layers = chooseHybridLayers(operation, *base, context);
  HybridComposition composition{std::move(*base), std::move(layers), context.config.hybridMode};
  return TransformPlan{&operation, TransformKind::Auto, 1, std::nullopt, std::move(composition)};
}

std::optional<TransformPlan> planStatefulHybridTransform(llvm::BinaryOperator &operation,
                                                         A2MBAContext &context,
                                                         const ProtectionProfile &profile) {
  hybrid::Options options = hybridOptions(context.config, profile, context.random);
  auto base = hybrid::plan(operation, options);
  hybrid::Plan selected;
  if (base) {
    selected = std::move(*base);
  } else {
    llvm::consumeError(base.takeError());
    auto fallback = makeSeedHybridPlan(operation, options);
    if (!fallback) {
      context.statistics.recordSkip(SkipReason::HybridPlanningFailure);
      diagnoseHybridSkip(context.config, operation, "seed fallback failed");
      return std::nullopt;
    }
    selected = std::move(*fallback);
  }

  hybrid::Layers layers = chooseHybridLayers(operation, selected, context);
  HybridComposition composition{std::move(selected), std::move(layers), context.config.hybridMode};
  return TransformPlan{&operation, TransformKind::Auto, 1, std::nullopt, std::move(composition)};
}

std::optional<TransformPlan> planTransform(llvm::BinaryOperator &operation, A2MBAContext &context) {
  const ProtectionProfile profile = context.config.profile();
  if (!takeOrFatal(context.random.chance(profile.candidateProbability))) {
    context.statistics.recordSkip(SkipReason::Probability);
    return std::nullopt;
  }

  if (context.config.hybridMode != HybridMode::Off) {
    return planHybridTransform(operation, context, profile);
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
  return TransformPlan{&operation, transform, depth, contextTrap, std::nullopt};
}

llvm::Value *mark(llvm::Value *value) {
  if (auto *instruction = llvm::dyn_cast<llvm::Instruction>(value)) {
    markGenerated(*instruction);
  }
  return value;
}

llvm::Value *stabilizeOperand(llvm::IRBuilderBase &builder, llvm::Value *value) {
  if (llvm::isGuaranteedNotToBeUndefOrPoison(value)) {
    return value;
  }
  // Undef may change at each use; the expanded identities need stable input bits.
  return mark(builder.CreateFreeze(value, "a2mba.input"));
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

void recordHybridPlanStatistics(const TransformPlan &plan, A2MBAContext &context) {
  if (plan.hybrid->mode == HybridMode::PureIR) {
    ++context.statistics.hybridIRTransforms;
  } else {
    ++context.statistics.hybridNativeTransforms;
  }
  context.statistics.contextTraps += plan.hybrid->layers.contextCuts.size();
  if (plan.hybrid->layers.context) {
    ++context.statistics.contextTraps;
  }
  if (plan.hybrid->layers.architectural) {
    recordTransform(context.statistics, plan.hybrid->layers.architectural->kind);
  }
}

void applyPlan(const TransformPlan &plan, A2MBAContext &context) {
  llvm::BinaryOperator &operation = *plan.operation;
  llvm::IRBuilder<> builder(&operation);
  builder.SetCurrentDebugLocation(operation.getDebugLoc());

  llvm::Value *left = operation.getOperand(0);
  llvm::Value *right = operation.getOperand(1);
  llvm::Value *stableLeft = stabilizeOperand(builder, left);
  llvm::Value *stableRight = left == right ? stableLeft : stabilizeOperand(builder, right);
  operation.setOperand(0, stableLeft);
  operation.setOperand(1, stableRight);

  llvm::Value *replacement = nullptr;
  if (plan.hybrid) {
    replacement =
        takeOrFatal(hybrid::emit(builder, operation, plan.hybrid->base, plan.hybrid->layers));
    recordHybridPlanStatistics(plan, context);
  } else {
    replacement = applyPrimaryTransform(builder, plan, context);
    for (unsigned layer = 1; layer < plan.depth; ++layer) {
      replacement = applyAdditionalLayer(builder, *replacement, plan, context);
    }
  }

  operation.replaceAllUsesWith(replacement);
  operation.eraseFromParent();
  ++context.statistics.instructionsTransformed;
}

bool isAvailableRegionInput(llvm::Value *value, const llvm::BinaryOperator &first,
                            const llvm::SmallPtrSetImpl<llvm::BinaryOperator *> &candidates) {
  auto *instruction = llvm::dyn_cast<llvm::Instruction>(value);
  if (!instruction) {
    return true;
  }
  if (auto *candidate = llvm::dyn_cast<llvm::BinaryOperator>(instruction);
      candidate && candidates.contains(candidate)) {
    return false;
  }
  // Inputs defined inside the block must precede the region's seed builder.
  return instruction->getParent() != first.getParent() || instruction->comesBefore(&first);
}

bool hasOnlyAvailableInputs(const llvm::BinaryOperator &operation,
                            const llvm::BinaryOperator &first, const llvm::Value *current,
                            const llvm::SmallPtrSetImpl<llvm::BinaryOperator *> &candidates) {
  const auto operands = operation.operands();
  return std::all_of(operands.begin(), operands.end(), [&](const llvm::Use &operand) {
    return operand.get() == current || isAvailableRegionInput(operand.get(), first, candidates);
  });
}

bool hasNoPhiUsers(const llvm::BinaryOperator &operation) {
  const auto users = operation.users();
  return std::none_of(users.begin(), users.end(),
                      [](const llvm::User *user) { return llvm::isa<llvm::PHINode>(user); });
}

llvm::SmallVector<StatefulRegion, 8> findStatefulRegions(llvm::Function &function) {
  llvm::SmallVector<llvm::BinaryOperator *, 64> candidates;
  llvm::SmallPtrSet<llvm::BinaryOperator *, 32> candidateSet;
  for (llvm::Instruction &instruction : llvm::instructions(function)) {
    const EligibilityResult candidate = checkCandidate(instruction);
    if (candidate) {
      candidates.push_back(candidate.operation);
      candidateSet.insert(candidate.operation);
    }
  }

  llvm::SmallPtrSet<llvm::BinaryOperator *, 32> visited;
  llvm::SmallVector<StatefulRegion, 8> regions;
  for (llvm::BinaryOperator *start : candidates) {
    if (visited.contains(start) || !hasOnlyAvailableInputs(*start, *start, nullptr, candidateSet)) {
      continue;
    }

    StatefulRegion region;
    region.operations.push_back(start);
    llvm::BinaryOperator *current = start;
    while (current->hasOneUse()) {
      auto *next = llvm::dyn_cast<llvm::BinaryOperator>(*current->user_begin());
      if (!next || next->getParent() != current->getParent() || !candidateSet.contains(next) ||
          visited.contains(next) ||
          (next->getOperand(0) != current && next->getOperand(1) != current) ||
          !hasOnlyAvailableInputs(*next, *start, current, candidateSet)) {
        break;
      }
      region.operations.push_back(next);
      current = next;
    }

    if (region.operations.size() < 2 || !hasNoPhiUsers(*region.operations.back())) {
      continue;
    }
    for (llvm::BinaryOperator *operation : region.operations) {
      visited.insert(operation);
    }
    regions.push_back(std::move(region));
  }
  return regions;
}

std::optional<StatefulRegionPlan> planStatefulRegion(const StatefulRegion &region,
                                                     A2MBAContext &context) {
  const ProtectionProfile profile = context.config.profile();
  StatefulRegionPlan planned;
  planned.region = region;
  for (llvm::BinaryOperator *operation : region.operations) {
    if (!takeOrFatal(context.random.chance(profile.candidateProbability))) {
      context.statistics.recordSkip(SkipReason::Probability);
      return std::nullopt;
    }
    auto plan = planStatefulHybridTransform(*operation, context, profile);
    if (!plan || !plan->hybrid) {
      return std::nullopt;
    }
    planned.transforms.push_back(std::move(*plan));
  }
  return planned;
}

llvm::Value *decodeStatefulOperand(llvm::IRBuilderBase &builder, llvm::Value *operand,
                                   llvm::ArrayRef<EncodedValue> encoded) {
  for (auto found = encoded.rbegin(); found != encoded.rend(); ++found) {
    if (found->source == operand) {
      auto *unscaled =
          mark(builder.CreateMul(found->value, found->inverse, "a2mba.region.decode.scale"));
      return mark(builder.CreateSub(unscaled, found->bias, "a2mba.region.decode"));
    }
  }
  return operand;
}

llvm::Value *createStateTransition(llvm::IRBuilderBase &builder, llvm::Value &state,
                                   llvm::Value &value, llvm::Value &contextValue,
                                   llvm::Value &salt) {
  auto *one = llvm::ConstantInt::get(contextValue.getType(), 1);
  auto *mixed = mark(builder.CreateXor(&state, &value, "a2mba.region.state.mix"));
  auto *context = mark(builder.CreateXor(&state, &contextValue, "a2mba.region.state.context"));
  auto *oddRight = mark(builder.CreateOr(context, one, "a2mba.region.state.multiplier"));
  auto *product = mark(builder.CreateMul(mixed, oddRight, "a2mba.region.state.product"));
  return mark(builder.CreateAdd(product, &salt, "a2mba.region.state.next"));
}

EncodedValue encodeStatefulValue(llvm::IRBuilderBase &builder, llvm::Value &source,
                                 llvm::Value &value, llvm::Value &state, llvm::Value &left,
                                 llvm::Value &right,
                                 const NonlinearEnvelopeParameters &parameters) {
  auto *integerType = llvm::cast<llvm::IntegerType>(value.getType());
  auto *firstSalt = llvm::ConstantInt::get(integerType, parameters.firstSalt);
  auto *secondSalt = llvm::ConstantInt::get(integerType, parameters.secondSalt);
  auto *one = llvm::ConstantInt::get(integerType, 1);

  auto *biasedState = mark(builder.CreateAdd(&state, firstSalt, "a2mba.region.bias.state"));
  auto *biasedOperand = mark(builder.CreateXor(&left, secondSalt, "a2mba.region.bias.operand"));
  auto *bias = mark(builder.CreateMul(biasedState, biasedOperand, "a2mba.region.bias"));

  auto *keyState = mark(builder.CreateXor(&state, secondSalt, "a2mba.region.key.state"));
  auto *keyOperand = mark(builder.CreateAdd(&right, firstSalt, "a2mba.region.key.operand"));
  auto *keyProduct = mark(builder.CreateMul(keyState, keyOperand, "a2mba.region.key.product"));
  auto *keyDoubled = mark(builder.CreateAdd(keyProduct, keyProduct, "a2mba.region.key.doubled"));
  auto *key = mark(builder.CreateOr(keyDoubled, one, "a2mba.region.key"));
  llvm::Value *inverse = createOddModularInverse(builder, *key, parameters);

  auto *biasedValue = mark(builder.CreateAdd(&value, bias, "a2mba.region.value.biased"));
  auto *encoded = mark(builder.CreateMul(biasedValue, key, "a2mba.region.encoded"));
  return {&source, encoded, inverse, bias};
}

void emitStatefulRegion(const StatefulRegionPlan &planned, A2MBAContext &context) {
  llvm::BinaryOperator &first = *planned.region.operations.front();
  llvm::Type *type = first.getType();
  const std::uint64_t seedSalt = takeOrFatal(context.random.next64());
  const std::uint64_t stateSalt = takeOrFatal(context.random.next64());
  auto *seedSaltValue = llvm::ConstantInt::get(type, seedSalt);
  auto *stateSaltValue = llvm::ConstantInt::get(type, stateSalt);
  auto *one = llvm::ConstantInt::get(type, 1);

  llvm::SmallVector<EncodedValue, 8> encoded;
  llvm::IRBuilder<> seedBuilder(&first);
  seedBuilder.SetCurrentDebugLocation(first.getDebugLoc());
  llvm::SmallDenseMap<llvm::Value *, llvm::Value *, 8> stableInputs;
  for (llvm::BinaryOperator *operation : planned.region.operations) {
    for (llvm::Use &operand : operation->operands()) {
      llvm::Value *value = operand.get();
      if (llvm::isa<llvm::Instruction>(value)) {
        continue;
      }
      auto [entry, inserted] = stableInputs.try_emplace(value, nullptr);
      if (inserted) {
        entry->second = stabilizeOperand(seedBuilder, value);
      }
      operand.set(entry->second);
    }
  }
  auto *seedLeft = mark(
      seedBuilder.CreateXor(first.getOperand(0), seedSaltValue, "a2mba.region.state.seed.left"));
  auto *seedRight =
      mark(seedBuilder.CreateOr(first.getOperand(1), one, "a2mba.region.state.seed.multiplier"));
  auto *seedProduct = mark(seedBuilder.CreateMul(seedLeft, seedRight, "a2mba.region.state.seed"));
  llvm::Value *state =
      mark(seedBuilder.CreateAdd(seedProduct, stateSaltValue, "a2mba.region.state.initial"));

  for (std::size_t index = 0; index < planned.transforms.size(); ++index) {
    const TransformPlan &transform = planned.transforms[index];
    llvm::BinaryOperator &operation = *transform.operation;
    llvm::IRBuilder<> builder(&operation);
    builder.SetCurrentDebugLocation(operation.getDebugLoc());
    llvm::Value *left = decodeStatefulOperand(builder, operation.getOperand(0), encoded);
    llvm::Value *right = decodeStatefulOperand(builder, operation.getOperand(1), encoded);
    llvm::Value *replacement = takeOrFatal(hybrid::emitWithOperands(
        builder, operation, transform.hybrid->base, *left, *right, transform.hybrid->layers));
    recordHybridPlanStatistics(transform, context);

    llvm::Value *nextState = nullptr;
    if (index + 1 == planned.transforms.size()) {
      nextState = createStateTransition(builder, *state, *replacement, *right, *stateSaltValue);
    } else {
      const std::uint64_t stepSalt =
          stateSalt ^ (0x9e3779b97f4a7c15ULL * static_cast<std::uint64_t>(index + 1));
      auto *stepSaltValue = llvm::ConstantInt::get(type, stepSalt);
      nextState = createStateTransition(builder, *state, *replacement, *right, *stepSaltValue);
    }
    encoded.push_back(encodeStatefulValue(builder, operation, *replacement, *nextState, *left,
                                          *right, transform.hybrid->layers.nonlinear));
    state = nextState;
  }

  for (std::size_t index = 0; index + 1 < planned.region.operations.size(); ++index) {
    llvm::BinaryOperator *operation = planned.region.operations[index];
    operation->replaceAllUsesWith(encoded[index].value);
  }

  llvm::BinaryOperator *last = planned.region.operations.back();
  llvm::IRBuilder<> exitBuilder(last);
  exitBuilder.SetCurrentDebugLocation(last->getDebugLoc());
  auto *unscaled = mark(exitBuilder.CreateMul(encoded.back().value, encoded.back().inverse,
                                              "a2mba.region.decode.exit.scale"));
  auto *decodedResult =
      mark(exitBuilder.CreateSub(unscaled, encoded.back().bias, "a2mba.region.decode.exit"));
  last->replaceAllUsesWith(decodedResult);

  for (auto operation = planned.region.operations.rbegin();
       operation != planned.region.operations.rend(); ++operation) {
    (*operation)->eraseFromParent();
  }
  ++context.statistics.statefulRegions;
  context.statistics.instructionsTransformed += planned.region.operations.size();
}

bool transformStatefulRegions(llvm::Function &function, A2MBAContext &context) {
  if (context.config.hybridRegion != HybridRegionMode::Stateful ||
      context.config.hybridMode == HybridMode::Off) {
    return false;
  }

  bool changed = false;
  for (const StatefulRegion &region : findStatefulRegions(function)) {
    auto planned = planStatefulRegion(region, context);
    if (!planned) {
      continue;
    }
    for (llvm::BinaryOperator *operation : region.operations) {
      ++context.statistics.instructionsVisited;
      ++context.statistics.candidates;
    }
    emitStatefulRegion(*planned, context);
    changed = true;
  }
  return changed;
}

bool transformFunction(llvm::Function &function, A2MBAContext &context) {
  bool changed = transformStatefulRegions(function, context);
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
      plans.push_back(std::move(*plan));
    }
  }

  for (const TransformPlan &plan : plans) {
    applyPlan(plan, context);
  }
  if (!plans.empty()) {
    markProtected(function);
    changed = true;
  }
  if (changed) {
    markProtected(function);
  }
  return changed;
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
