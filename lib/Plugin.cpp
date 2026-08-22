#include "a2mba/A2MBA.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/ErrorHandling.h"

#ifdef A2MBA_STATIC_LLVM_PLUGIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace llvm;

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
#ifdef A2MBA_STATIC_LLVM_PLUGIN
  // The portable Windows LLVM SDK builds opt and this compatibility module
  // against separate static LLVM copies. InlineAsm instances created by the
  // module can outlive PassPlugin's DynamicLibrary handle, so keep the module
  // resident until process shutdown.
  HMODULE module = nullptr;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCSTR>(&llvmGetPassPluginInfo), &module)) {
    llvm::report_fatal_error(llvm::Twine("A2MBA: could not pin the static LLVM compatibility DLL; "
                                         "GetLastError=") +
                                 llvm::Twine(static_cast<unsigned>(GetLastError())),
                             false);
  }
#endif

  return {LLVM_PLUGIN_API_VERSION, a2mba::PluginName, a2mba::VersionString,
          [](PassBuilder &passBuilder) {
            passBuilder.registerPipelineParsingCallback([](StringRef name,
                                                           ModulePassManager &modulePassManager,
                                                           ArrayRef<PassBuilder::PipelineElement>) {
              if (name != a2mba::PassPipelineName) {
                return false;
              }
              modulePassManager.addPass(a2mba::A2MBAPass());
              return true;
            });

            passBuilder.registerOptimizerLastEPCallback([](ModulePassManager &modulePassManager,
                                                           OptimizationLevel level,
                                                           ThinOrFullLTOPhase phase) {
              if (level == OptimizationLevel::O0 || phase != ThinOrFullLTOPhase::None) {
                return;
              }
              modulePassManager.addPass(a2mba::A2MBAPass());
            });
          }};
}
