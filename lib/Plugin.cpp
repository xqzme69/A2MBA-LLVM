#include "a2mba/A2MBA.h"

#include "PassRegistration.h"

#include "llvm/Passes/PassPlugin.h"

extern "C" LLVM_ATTRIBUTE_WEAK llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, a2mba::PluginName, a2mba::VersionString, a2mba::registerPasses};
}
