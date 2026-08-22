#include "a2mba/AAMBA.h"

#include "a2mba/Metadata.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Errc.h"
#include "llvm/TargetParser/Triple.h"

#include <string>

namespace a2mba {
namespace {

struct AssemblyPrimitive {
  std::string instructions;
  std::string constraints;
  bool usesConstant;
  const char *name;
};

llvm::Expected<AssemblyPrimitive> describePrimitive(unsigned bitWidth, TransformKind transform,
                                                    bool protectRedZoneInline) {
  const char widthSuffix = bitWidth == 32 ? 'l' : 'q';
  const char *registerModifier = bitWidth == 32 ? "k" : "q";
  const std::string saveFlags =
      protectRedZoneInline ? "leaq -128(%rsp), %rsp\n\tpushfq\n\t" : "pushfq\n\t";
  const std::string restoreFlags = protectRedZoneInline ? "popfq\n\tleaq 128(%rsp), %rsp" : "popfq";

  switch (transform) {
  case TransformKind::Adc:
    return AssemblyPrimitive{saveFlags + "stc\n\tadc" + std::string(1, widthSuffix) +
                                 " ${2:" + registerModifier + "}, ${0:" + registerModifier +
                                 "}\n\tsub" + widthSuffix + " ${2:" + registerModifier +
                                 "}, ${0:" + registerModifier + "}\n\tsub" + widthSuffix +
                                 " $$1, ${0:" + registerModifier + "}\n\t" + restoreFlags,
                             "=&r,0,r,~{memory},~{flags}", true, "a2mba.adc"};
  case TransformKind::Sbb:
    return AssemblyPrimitive{saveFlags + "stc\n\tsbb" + std::string(1, widthSuffix) +
                                 " ${2:" + registerModifier + "}, ${0:" + registerModifier +
                                 "}\n\tadd" + widthSuffix + " ${2:" + registerModifier +
                                 "}, ${0:" + registerModifier + "}\n\tadd" + widthSuffix +
                                 " $$1, ${0:" + registerModifier + "}\n\t" + restoreFlags,
                             "=&r,0,r,~{memory},~{flags}", true, "a2mba.sbb"};
  case TransformKind::PaperRcrRcl:
    return AssemblyPrimitive{saveFlags + "stc\n\trcl" + std::string(1, widthSuffix) +
                                 " $$1, ${0:" + registerModifier + "}\n\trcr" + widthSuffix +
                                 " $$1, ${0:" + registerModifier + "}\n\t" + restoreFlags,
                             "=&r,0,~{memory},~{flags}", false, "a2mba.paper.rotate"};
  default:
    return llvm::createStringError(llvm::errc::invalid_argument, "unsupported AAMBA transform: %s",
                                   toString(transform).str().c_str());
  }
}

} // namespace

llvm::Expected<llvm::Value *> applyArchitecturalIdentity(llvm::IRBuilderBase &builder,
                                                         llvm::Value &input,
                                                         const llvm::APInt &constant,
                                                         TransformKind transform) {
  auto *integerType = llvm::dyn_cast<llvm::IntegerType>(input.getType());
  if (!integerType || (integerType->getBitWidth() != 32 && integerType->getBitWidth() != 64)) {
    return llvm::createStringError(llvm::errc::invalid_argument, "AAMBA requires i32 or i64");
  }
  if (constant.getBitWidth() != integerType->getBitWidth()) {
    return llvm::createStringError(llvm::errc::invalid_argument, "AAMBA constant width mismatch");
  }

  llvm::Function &function = *builder.GetInsertBlock()->getParent();
  const llvm::Triple triple(function.getParent()->getTargetTriple());
  bool protectRedZoneInline = false;
#ifdef A2MBA_STATIC_LLVM_PLUGIN
  // LLVM's official portable Windows SDK disables plugins. Its compatibility
  // fallback links LLVM into this DLL, so AttributeList objects cannot safely
  // cross into opt's separately linked LLVMContext. Keep PUSHFQ outside the
  // SysV red zone with flag-neutral LEA stack adjustments instead.
  protectRedZoneInline = triple.isOSLinux();
#else
  if (triple.isOSLinux()) {
    function.addFnAttr(llvm::Attribute::NoRedZone);
  }
#endif

  auto primitive = describePrimitive(integerType->getBitWidth(), transform, protectRedZoneInline);
  if (!primitive) {
    return primitive.takeError();
  }

  llvm::SmallVector<llvm::Type *, 2> argumentTypes{integerType};
  llvm::SmallVector<llvm::Value *, 2> arguments{&input};
  if (primitive->usesConstant) {
    argumentTypes.push_back(integerType);
    arguments.push_back(llvm::ConstantInt::get(integerType, constant));
  }

  auto *functionType = llvm::FunctionType::get(integerType, argumentTypes, false);
  auto *assembly = llvm::InlineAsm::get(
      functionType, primitive->instructions, primitive->constraints,
      /*hasSideEffects=*/true, /*isAlignStack=*/false, llvm::InlineAsm::AD_ATT, /*canThrow=*/false);
  auto *call = builder.CreateCall(functionType, assembly, arguments, primitive->name);
  markGenerated(*call);
  return call;
}

} // namespace a2mba
