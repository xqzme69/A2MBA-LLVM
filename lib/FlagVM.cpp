#include "FlagVM.h"

#include "a2mba/Metadata.h"
#include "a2mba/Random.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace a2mba {
namespace {

enum class FlagKind : unsigned { Carry, Parity, AuxiliaryCarry, Zero, Sign, Overflow };
enum class FlagOp : unsigned {
  Load,
  InitCarrier,
  AddAdc,
  AddSbb,
  AddSet,
  AddSetInverse,
  AddCompare,
  AddAuxiliaryShift,
  AddAuxiliaryMask,
  Count,
};

constexpr unsigned opcodeCount = static_cast<unsigned>(FlagOp::Count);

struct FlagProgram {
  FlagKind flag;
  unsigned sumOutput;
  unsigned firstInput;
  bool zeroWithXor;
  std::array<std::uint8_t, opcodeCount> opcodeMap;
  std::array<std::uint8_t, 3> bytecode;
};

unsigned opcodeIndex(FlagOp op) { return static_cast<unsigned>(op); }

llvm::Expected<FlagProgram> makeFlagProgram(std::uint64_t siteSeed) {
  RandomSource random = RandomSource::deterministic(siteSeed);
  FlagProgram program{};
  auto flag = random.uniform(6);
  if (!flag)
    return flag.takeError();
  program.flag = static_cast<FlagKind>(*flag);

  auto sumOutput = random.uniform(2);
  if (!sumOutput)
    return sumOutput.takeError();
  program.sumOutput = static_cast<unsigned>(*sumOutput);

  auto firstInput = random.uniform(2);
  if (!firstInput)
    return firstInput.takeError();
  program.firstInput = static_cast<unsigned>(*firstInput);

  auto zeroWithXor = random.uniform(2);
  if (!zeroWithXor)
    return zeroWithXor.takeError();
  program.zeroWithXor = *zeroWithXor != 0;

  for (unsigned index = 0; index < opcodeCount; ++index)
    program.opcodeMap[index] = static_cast<std::uint8_t>(index);
  for (unsigned count = opcodeCount; count > 1; --count) {
    auto selected = random.uniform(count);
    if (!selected)
      return selected.takeError();
    std::swap(program.opcodeMap[count - 1], program.opcodeMap[*selected]);
  }

  const unsigned forms = program.flag == FlagKind::Carry ? 5 : 2;
  auto selected = random.uniform(forms);
  if (!selected)
    return selected.takeError();
  FlagOp capture = *selected ? FlagOp::AddSetInverse : FlagOp::AddSet;
  if (program.flag == FlagKind::Carry)
    capture = static_cast<FlagOp>(opcodeIndex(FlagOp::AddAdc) + *selected);
  else if (program.flag == FlagKind::AuxiliaryCarry)
    capture = *selected ? FlagOp::AddAuxiliaryMask : FlagOp::AddAuxiliaryShift;
  program.bytecode = {program.opcodeMap[opcodeIndex(FlagOp::Load)],
                      program.opcodeMap[opcodeIndex(FlagOp::InitCarrier)],
                      program.opcodeMap[opcodeIndex(capture)]};
  return program;
}

const char *conditionCode(FlagKind flag, bool inverse) {
  switch (flag) {
  case FlagKind::Carry:
    return inverse ? "nc" : "c";
  case FlagKind::Parity:
    return inverse ? "np" : "p";
  case FlagKind::AuxiliaryCarry:
    llvm_unreachable("auxiliary carry has no setcc form");
  case FlagKind::Zero:
    return inverse ? "nz" : "z";
  case FlagKind::Sign:
    return inverse ? "ns" : "s";
  case FlagKind::Overflow:
    return inverse ? "no" : "o";
  }
  llvm_unreachable("invalid flag kind");
}

std::string renderFlagProgram(const FlagProgram &program, unsigned width) {
  const char *suffix = width == 32 ? "l" : "q";
  const char *modifier = width == 32 ? "k" : "q";
  auto reg = [&](unsigned index) { return "${" + std::to_string(index) + ':' + modifier + '}'; };
  const std::string sum = reg(program.sumOutput);
  const std::string flag = reg(1 - program.sumOutput);
  const std::string first = reg(2 + program.firstInput);
  const std::string second = reg(3 - program.firstInput);
  const std::string flagByte = "${" + std::to_string(1 - program.sumOutput) + ":b}";

  std::ostringstream text;
  for (std::uint8_t encoded : program.bytecode) {
    auto found = std::find(program.opcodeMap.begin(), program.opcodeMap.end(), encoded);
    if (found == program.opcodeMap.end())
      llvm_unreachable("flag VM opcode is not in the site ISA");
    const auto op = static_cast<FlagOp>(found - program.opcodeMap.begin());
    switch (op) {
    case FlagOp::Load:
      text << "mov" << suffix << ' ' << first << ", " << sum << "\n\t";
      break;
    case FlagOp::InitCarrier:
      if (program.flag == FlagKind::AuxiliaryCarry)
        text << "mov" << suffix << ' ' << first << ", " << flag << "\n\t";
      else if (program.zeroWithXor)
        text << "xor" << suffix << ' ' << flag << ", " << flag << "\n\t";
      else
        text << "mov" << suffix << " $$0, " << flag << "\n\t";
      break;
    case FlagOp::AddAdc:
      text << "add" << suffix << ' ' << second << ", " << sum << "\n\t"
           << "adc" << suffix << " $$0, " << flag << "\n\t";
      break;
    case FlagOp::AddSbb:
      text << "add" << suffix << ' ' << second << ", " << sum << "\n\t"
           << "sbb" << suffix << ' ' << flag << ", " << flag << "\n\t"
           << "neg" << suffix << ' ' << flag << "\n\t";
      break;
    case FlagOp::AddSet:
    case FlagOp::AddSetInverse:
      text << "add" << suffix << ' ' << second << ", " << sum << "\n\t"
           << "set" << conditionCode(program.flag, op == FlagOp::AddSetInverse) << ' ' << flagByte
           << "\n\t";
      if (op == FlagOp::AddSetInverse)
        text << "xor" << suffix << " $$1, " << flag << "\n\t";
      break;
    case FlagOp::AddCompare:
      text << "add" << suffix << ' ' << second << ", " << sum << "\n\t"
           << "cmp" << suffix << ' ' << first << ", " << sum << "\n\t"
           << "setb " << flagByte << "\n\t";
      break;
    case FlagOp::AddAuxiliaryShift:
    case FlagOp::AddAuxiliaryMask:
      text << "add" << suffix << ' ' << second << ", " << sum << "\n\t"
           << "xor" << suffix << ' ' << second << ", " << flag << "\n\t"
           << "xor" << suffix << ' ' << sum << ", " << flag << "\n\t";
      if (op == FlagOp::AddAuxiliaryMask)
        text << "and" << suffix << " $$16, " << flag << "\n\t";
      text << "shr" << suffix << " $$4, " << flag << "\n\t";
      if (op == FlagOp::AddAuxiliaryShift)
        text << "and" << suffix << " $$1, " << flag << "\n\t";
      break;
    case FlagOp::Count:
      llvm_unreachable("invalid flag VM opcode");
    }
  }
  return text.str();
}

} // namespace

llvm::Expected<FlagVMResult> emitFlagVM(llvm::IRBuilderBase &builder, llvm::Value &state,
                                        llvm::Value &value, std::uint64_t siteSeed) {
  auto *integerType = llvm::dyn_cast<llvm::IntegerType>(state.getType());
  if (!integerType || integerType != value.getType() ||
      (integerType->getBitWidth() != 32 && integerType->getBitWidth() != 64)) {
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "flag VM requires matching i32 or i64 operands");
  }
  auto program = makeFlagProgram(siteSeed);
  if (!program)
    return program.takeError();

  auto *returnType = llvm::StructType::get(builder.getContext(), {integerType, integerType});
  auto *functionType = llvm::FunctionType::get(returnType, {integerType, integerType}, false);
  constexpr const char *constraints = "=&r,=&r,r,r,~{flags}";
  if (auto error = llvm::InlineAsm::verify(functionType, constraints))
    return std::move(error);

  auto *assembly =
      llvm::InlineAsm::get(functionType, renderFlagProgram(*program, integerType->getBitWidth()),
                           constraints, /*hasSideEffects=*/false,
                           /*isAlignStack=*/false, llvm::InlineAsm::AD_ATT,
                           /*canThrow=*/false);
  auto *call = builder.CreateCall(functionType, assembly, {&state, &value}, "a2mba.flagvm");
  markGenerated(*call);
  auto *sum = builder.CreateExtractValue(call, {program->sumOutput}, "a2mba.flagvm.sum");
  auto *flag = builder.CreateExtractValue(call, {1 - program->sumOutput}, "a2mba.flagvm.flag");
  if (auto *instruction = llvm::dyn_cast<llvm::Instruction>(sum))
    markGenerated(*instruction);
  if (auto *instruction = llvm::dyn_cast<llvm::Instruction>(flag))
    markGenerated(*instruction);
  return FlagVMResult{sum, flag};
}

} // namespace a2mba
