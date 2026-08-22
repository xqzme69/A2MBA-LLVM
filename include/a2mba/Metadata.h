#pragma once

#include "llvm/ADT/DenseSet.h"

namespace llvm {
class Function;
class Instruction;
class Module;
} // namespace llvm

namespace a2mba {

inline constexpr char GeneratedMetadata[] = "a2mba.generated";
inline constexpr char ProtectedMetadata[] = "a2mba.protected";
inline constexpr char ProcessedMetadata[] = "a2mba.processed";

struct FunctionAnnotations {
  llvm::DenseSet<const llvm::Function *> protectedFunctions;
  llvm::DenseSet<const llvm::Function *> ignoredFunctions;
};

FunctionAnnotations collectFunctionAnnotations(llvm::Module &module);

bool isGenerated(const llvm::Instruction &instruction);
void markGenerated(llvm::Instruction &instruction);
void markProtected(llvm::Function &function);

bool isModuleProcessed(const llvm::Module &module);
void markModuleProcessed(llvm::Module &module);

} // namespace a2mba
