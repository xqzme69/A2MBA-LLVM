#include "a2mba/HybridCore.h"

#include <algorithm>
#include <sstream>

namespace a2mba::hybrid_core {

unsigned arity(Op op) noexcept {
  switch (op) {
  case Op::Input:
  case Op::Constant:
    return 0;
  case Op::Not:
  case Op::Neg:
    return 1;
  case Op::Add:
  case Op::Sub:
  case Op::Mul:
  case Op::And:
  case Op::Or:
  case Op::Xor:
    return 2;
  }
  return 3;
}

const char *opName(Op op) noexcept {
  switch (op) {
  case Op::Input:
    return "input";
  case Op::Constant:
    return "const";
  case Op::Add:
    return "add";
  case Op::Sub:
    return "sub";
  case Op::Mul:
    return "mul";
  case Op::And:
    return "and";
  case Op::Or:
    return "or";
  case Op::Xor:
    return "xor";
  case Op::Not:
    return "not";
  case Op::Neg:
    return "neg";
  }
  return "invalid";
}

std::uint64_t widthMask(Width width) noexcept {
  return width == Width::W32 ? UINT64_C(0xffffffff) : UINT64_MAX;
}

std::string validate(const Program &program) {
  if (program.width != Width::W32 && program.width != Width::W64) {
    return "only i32/i64 are supported";
  }
  if (program.nodes.empty() || program.nodes.size() > 4096 ||
      program.root >= program.nodes.size()) {
    return "invalid DAG size/root";
  }
  for (Id index = 0; index < program.nodes.size(); ++index) {
    const auto &node = program.nodes[index];
    const unsigned operandCount = arity(node.op);
    if (operandCount > 2) {
      return "invalid opcode";
    }
    if ((operandCount > 0 && node.left >= index) || (operandCount > 1 && node.right >= index)) {
      return "DAG contains a forward/cyclic reference";
    }
    if ((operandCount == 0 && node.left != invalidId) ||
        (operandCount < 2 && node.right != invalidId)) {
      return "unused child is not empty";
    }
    if (node.op == Op::Input && node.payload > 1) {
      return "input index must be 0 or 1";
    }
    if (node.op == Op::Constant && (node.payload & ~widthMask(program.width))) {
      return "constant does not fit width";
    }
    if (operandCount && node.payload != 0) {
      return "operator payload must be zero";
    }
  }
  return {};
}

Result<Program> makeSeed(Op op, Width width) {
  if (arity(op) != 2) {
    return Result<Program>::fail("seed must be a binary operation");
  }
  Program program{
      width,
      {{Op::Input, invalidId, invalidId, 0}, {Op::Input, invalidId, invalidId, 1}, {op, 0, 1, 0}},
      2};
  if (auto reason = validate(program); !reason.empty()) {
    return Result<Program>::fail(reason);
  }
  return Result<Program>::ok(std::move(program));
}

Result<std::uint64_t> evaluate(const Program &program, std::uint64_t leftInput,
                               std::uint64_t rightInput) {
  if (auto reason = validate(program); !reason.empty()) {
    return Result<std::uint64_t>::fail(reason);
  }
  std::vector<std::uint64_t> values;
  values.reserve(program.nodes.size());
  const auto mask = widthMask(program.width);
  for (const auto &node : program.nodes) {
    const auto operandCount = arity(node.op);
    const auto left = operandCount ? values[node.left] : 0;
    const auto right = operandCount == 2 ? values[node.right] : 0;
    std::uint64_t value = 0;
    switch (node.op) {
    case Op::Input:
      value = node.payload ? rightInput : leftInput;
      break;
    case Op::Constant:
      value = node.payload;
      break;
    case Op::Add:
      value = left + right;
      break;
    case Op::Sub:
      value = left - right;
      break;
    case Op::Mul:
      value = left * right;
      break;
    case Op::And:
      value = left & right;
      break;
    case Op::Or:
      value = left | right;
      break;
    case Op::Xor:
      value = left ^ right;
      break;
    case Op::Not:
      value = ~left;
      break;
    case Op::Neg:
      value = UINT64_C(0) - left;
      break;
    }
    values.push_back(value & mask);
  }
  return Result<std::uint64_t>::ok(values[program.root]);
}

std::string format(const Program &program) {
  if (!validate(program).empty()) {
    return "<invalid program>";
  }
  std::ostringstream output;
  for (Id index = 0; index < program.nodes.size(); ++index) {
    const auto &node = program.nodes[index];
    output << '%' << index << " = " << opName(node.op);
    if (!arity(node.op)) {
      output << ' ' << node.payload;
    } else {
      output << " %" << node.left;
      if (arity(node.op) == 2) {
        output << ", %" << node.right;
      }
    }
    output << '\n';
  }
  output << "return %" << program.root << '\n';
  return output.str();
}

Result<Prepared> prepareNative(const Program &seed, const SearchOptions &searchOptions,
                               const NativeOptions &nativeOptions) {
  auto result = search(seed, searchOptions);
  if (!result) {
    return Result<Prepared>::fail(result.error);
  }
  unsigned rejected = 0;
  for (auto &candidate : result.value->candidates) {
    auto plan = lowerNative(candidate.program, nativeOptions);
    if (!plan) {
      ++rejected;
      continue;
    }
    return Result<Prepared>::ok(
        {std::move(candidate), std::move(*plan.value), std::move(result.value->report), rejected});
  }
  return Result<Prepared>::fail(
      "no extracted candidate fits native budgets; caller must skip or fail");
}

} // namespace a2mba::hybrid_core
