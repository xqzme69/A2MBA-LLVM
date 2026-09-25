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
                                                    std::uint64_t variantSeed) {
  const char widthSuffix = bitWidth == 32 ? 'l' : 'q';
  const char *registerModifier = bitWidth == 32 ? "k" : "q";
  const bool preserveFlags = ((variantSeed >> 5U) & 1U) != 0;
  const std::string saveFlags = preserveFlags ? "pushfq\n\t" : "";
  const std::string restoreFlags = preserveFlags ? "\n\tpopfq" : "";

  std::string setCarry;
  switch ((variantSeed >> 1U) & 3U) {
  case 0:
    setCarry = "stc";
    break;
  case 1:
    setCarry = "clc\n\tcmc";
    break;
  case 2:
    setCarry = "bt" + std::string(1, widthSuffix) + " $$0, ${2:" + registerModifier + "}";
    break;
  case 3:
    setCarry = "cmp" + std::string(1, widthSuffix) + " ${2:" + registerModifier +
               "}, ${2:" + registerModifier + "}\n\tcmc";
    break;
  }

  const unsigned compensationVariant = static_cast<unsigned>((variantSeed >> 3U) % 3U);
  auto compensate = [&](llvm::StringRef operation, llvm::StringRef unitOperation) {
    const std::string source =
        " ${2:" + std::string(registerModifier) + "}, ${0:" + registerModifier + "}";
    const std::string unit = " $$1, ${0:" + std::string(registerModifier) + "}";
    const std::string unary = " ${0:" + std::string(registerModifier) + "}";
    if (compensationVariant == 0) {
      return operation.str() + widthSuffix + source + "\n\t" + operation.str() + widthSuffix + unit;
    }
    if (compensationVariant == 1) {
      return operation.str() + widthSuffix + unit + "\n\t" + operation.str() + widthSuffix + source;
    }
    return operation.str() + widthSuffix + source + "\n\t" + unitOperation.str() + widthSuffix +
           unary;
  };

  switch (transform) {
  case TransformKind::Adc: {
    const std::string body = saveFlags + setCarry + "\n\tadc" + widthSuffix +
                             " ${2:" + registerModifier + "}, ${0:" + registerModifier + "}\n\t" +
                             compensate("sub", "dec") + restoreFlags;
    return AssemblyPrimitive{body, "=&r,0,r,~{memory},~{flags}", true, "a2mba.adc"};
  }
  case TransformKind::Sbb: {
    const std::string body = saveFlags + setCarry + "\n\tsbb" + widthSuffix +
                             " ${2:" + registerModifier + "}, ${0:" + registerModifier + "}\n\t" +
                             compensate("add", "inc") + restoreFlags;
    return AssemblyPrimitive{body, "=&r,0,r,~{memory},~{flags}", true, "a2mba.sbb"};
  }
  case TransformKind::PaperRcrRcl:
    return AssemblyPrimitive{"pushfq\n\tstc\n\trcl" + std::string(1, widthSuffix) +
                                 " $$1, ${0:" + registerModifier + "}\n\trcr" + widthSuffix +
                                 " $$1, ${0:" + registerModifier + "}\n\tpopfq",
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
  if (triple.isOSLinux()) {
    function.addFnAttr(llvm::Attribute::NoRedZone);
  }

  auto primitive =
      describePrimitive(integerType->getBitWidth(), transform, constant.getZExtValue());
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
