#pragma once

#include "a2mba/AGT.h"
#include "a2mba/Config.h"
#include "a2mba/HybridCore.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace llvm {
class BinaryOperator;
}

namespace a2mba::hybrid {

enum class EmissionMode {
  PureIR,
  NativeRegisters,
};

struct Options {
  hybrid_core::SearchOptions search;
  hybrid_core::NativeOptions native;
  EmissionMode emission = EmissionMode::PureIR;
};

struct Plan {
  unsigned originalOpcode = 0;
  hybrid_core::Program expression;
  std::optional<hybrid_core::NativePlan> native;
  hybrid_core::SearchReport report;
};

struct ArchitecturalLayer {
  TransformKind kind = TransformKind::Adc;
  std::uint64_t constant = 1;
};

struct ContextCut {
  hybrid_core::Id node;
  ContextTrapParameters parameters;
};

struct Layers {
  std::vector<ContextCut> contextCuts;
  std::optional<ContextTrapParameters> context;
  std::optional<ArchitecturalLayer> architectural;
};

llvm::Expected<Plan> plan(llvm::BinaryOperator &operation, const Options &options);
llvm::Expected<llvm::Value *> emit(llvm::IRBuilderBase &builder, llvm::BinaryOperator &operation,
                                   const Plan &plan, const Layers &layers = {});

} // namespace a2mba::hybrid
