#include "a2mba/Metadata.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

namespace a2mba {
namespace {

llvm::StringRef getGlobalCString(llvm::Value *value) {
  value = value->stripPointerCasts();
  const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(value);
  if (!global || !global->hasInitializer()) {
    return {};
  }

  const auto *characters = llvm::dyn_cast<llvm::ConstantDataArray>(global->getInitializer());
  if (!characters || !characters->isCString()) {
    return {};
  }
  return characters->getAsCString();
}

void recordAnnotation(FunctionAnnotations &annotations, llvm::Value *target,
                      llvm::StringRef annotation) {
  auto *function = llvm::dyn_cast<llvm::Function>(target->stripPointerCasts());
  if (!function) {
    return;
  }

  if (annotation == "a2mba") {
    annotations.protectedFunctions.insert(function);
  } else if (annotation == "a2mba.ignore") {
    annotations.ignoredFunctions.insert(function);
  }
}

} // namespace

FunctionAnnotations collectFunctionAnnotations(llvm::Module &module) {
  FunctionAnnotations annotations;

  for (llvm::Function &function : module) {
    if (function.hasFnAttribute("a2mba")) {
      annotations.protectedFunctions.insert(&function);
    }
    if (function.hasFnAttribute("a2mba.ignore")) {
      annotations.ignoredFunctions.insert(&function);
    }
  }

  auto *globalAnnotations = module.getGlobalVariable("llvm.global.annotations");
  if (!globalAnnotations || !globalAnnotations->hasInitializer()) {
    return annotations;
  }

  const auto *array = llvm::dyn_cast<llvm::ConstantArray>(globalAnnotations->getInitializer());
  if (!array) {
    return annotations;
  }

  for (const llvm::Use &operand : array->operands()) {
    const auto *entry = llvm::dyn_cast<llvm::ConstantStruct>(operand.get());
    if (!entry || entry->getNumOperands() < 2) {
      continue;
    }
    recordAnnotation(annotations, entry->getOperand(0), getGlobalCString(entry->getOperand(1)));
  }

  return annotations;
}

bool isGenerated(const llvm::Instruction &instruction) {
  return instruction.getMetadata(GeneratedMetadata) != nullptr;
}

void markGenerated(llvm::Instruction &instruction) {
  instruction.setMetadata(GeneratedMetadata, llvm::MDNode::get(instruction.getContext(), {}));
}

void markProtected(llvm::Function &function) {
  function.setMetadata(ProtectedMetadata, llvm::MDNode::get(function.getContext(), {}));
}

bool isModuleProcessed(const llvm::Module &module) {
  return module.getNamedMetadata(ProcessedMetadata) != nullptr;
}

void markModuleProcessed(llvm::Module &module) {
  auto *metadata = module.getOrInsertNamedMetadata(ProcessedMetadata);
  if (metadata->getNumOperands() == 0) {
    metadata->addOperand(llvm::MDNode::get(module.getContext(), {}));
  }
}

} // namespace a2mba
