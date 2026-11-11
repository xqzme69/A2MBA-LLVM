#pragma once

#include "Hybrid.h"
#include "llvm/Support/Error.h"

namespace llvm {
class Function;
}

namespace a2mba {
class A2MBAContext;
struct HybridComposition {
  hybrid::Plan base;
  hybrid::Layers layers;
  HybridMode mode;
};
std::optional<HybridComposition> planCoordinateRoot(llvm::BinaryOperator &operation,
                                                    A2MBAContext &context);
llvm::Expected<bool> transformCoordinateRegions(llvm::Function &function, A2MBAContext &context);
} // namespace a2mba
