#include "PassRegistration.h"

#include "a2mba/A2MBA.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Passes/PassBuilder.h"

namespace a2mba {

void registerPasses(llvm::PassBuilder &passBuilder) {
  passBuilder.registerPipelineParsingCallback(
      [](llvm::StringRef name, llvm::ModulePassManager &modulePassManager,
         llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
        if (name != PassPipelineName) {
          return false;
        }
        modulePassManager.addPass(A2MBAPass());
        return true;
      });

  passBuilder.registerOptimizerLastEPCallback([](llvm::ModulePassManager &modulePassManager,
                                                 llvm::OptimizationLevel level,
                                                 llvm::ThinOrFullLTOPhase phase) {
    if (level == llvm::OptimizationLevel::O0 || phase != llvm::ThinOrFullLTOPhase::None) {
      return;
    }
    modulePassManager.addPass(A2MBAPass());
  });
}

} // namespace a2mba
