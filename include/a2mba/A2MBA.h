#pragma once

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
} // namespace llvm

namespace a2mba {

#ifndef A2MBA_VERSION_STRING
#define A2MBA_VERSION_STRING "0.1.0-dev"
#endif

inline constexpr char PassPipelineName[] = "a2mba";
inline constexpr char PluginName[] = "A2MBA";
inline constexpr char VersionString[] = A2MBA_VERSION_STRING;

class A2MBAPass : public llvm::PassInfoMixin<A2MBAPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &module, llvm::ModuleAnalysisManager &analysisManager);
};

} // namespace a2mba
