#include "a2mba/HybridCore.h"

#include <algorithm>
#include <array>
#include <sstream>

namespace a2mba::hybrid_core {

const char *machineOpName(MachineOp op) noexcept {
  switch (op) {
  case MachineOp::Mov:
    return "mov";
  case MachineOp::Add:
    return "add";
  case MachineOp::Sub:
    return "sub";
  case MachineOp::IMul:
    return "imul";
  case MachineOp::And:
    return "and";
  case MachineOp::Or:
    return "or";
  case MachineOp::Xor:
    return "xor";
  case MachineOp::Not:
    return "not";
  case MachineOp::Neg:
    return "neg";
  }
  return "invalid";
}

namespace {

bool unaryMachine(MachineOp op) { return op == MachineOp::Not || op == MachineOp::Neg; }

MachineOp machineOp(Op op) {
  switch (op) {
  case Op::Add:
    return MachineOp::Add;
  case Op::Sub:
    return MachineOp::Sub;
  case Op::Mul:
    return MachineOp::IMul;
  case Op::And:
    return MachineOp::And;
  case Op::Or:
    return MachineOp::Or;
  case Op::Xor:
    return MachineOp::Xor;
  case Op::Not:
    return MachineOp::Not;
  case Op::Neg:
    return MachineOp::Neg;
  default:
    return MachineOp::Mov;
  }
}

bool commutative(Op op) {
  return op == Op::Add || op == Op::Mul || op == Op::And || op == Op::Or || op == Op::Xor;
}

} // namespace

Result<NativePlan> lowerNative(const Program &program, const NativeOptions &options) {
  if (auto reason = validate(program); !reason.empty()) {
    return Result<NativePlan>::fail(reason);
  }
  if (!options.maxScratchRegisters || options.maxScratchRegisters > 6 || !options.maxInstructions ||
      options.maxInstructions > 1024) {
    return Result<NativePlan>::fail("invalid native budgets");
  }
  NativePlan plan;
  plan.width = program.width;
  std::vector<unsigned> uses(program.nodes.size(), 0);
  std::vector<bool> reachable(program.nodes.size(), false);
  reachable[program.root] = true;
  for (std::size_t index = program.nodes.size(); index-- > 0;) {
    if (!reachable[index]) {
      continue;
    }
    const auto &node = program.nodes[index];
    if (arity(node.op)) {
      reachable[node.left] = true;
      ++uses[node.left];
    }
    if (arity(node.op) == 2) {
      reachable[node.right] = true;
      ++uses[node.right];
    }
  }
  std::vector<Operand> locations(program.nodes.size());
  std::vector<bool> busy(options.maxScratchRegisters, false);
  auto allocate = [&]() -> std::optional<unsigned> {
    for (unsigned index = 0; index < busy.size(); ++index) {
      if (!busy[index]) {
        busy[index] = true;
        plan.scratchRegisters = std::max(plan.scratchRegisters, index + 1);
        return index;
      }
    }
    return {};
  };
  auto append = [&](MachineOp op, unsigned destination, Operand source = {}) {
    plan.instructions.push_back({op, destination, source});
  };
  for (Id id = 0; id < program.nodes.size(); ++id) {
    if (!reachable[id]) {
      continue;
    }
    const Node &node = program.nodes[id];
    if (node.op == Op::Input) {
      locations[id] = {OperandKind::Input, node.payload};
      continue;
    }
    if (node.op == Op::Constant) {
      auto destination = allocate();
      if (!destination) {
        return Result<NativePlan>::fail("scratch-register budget exceeded");
      }
      append(MachineOp::Mov, *destination, {OperandKind::Immediate, node.payload});
      locations[id] = {OperandKind::Scratch, *destination};
      continue;
    }
    Id leftId = node.left;
    Id rightId = node.right;
    auto dies = [&](Id child) {
      unsigned consumed = 1 + (arity(node.op) == 2 && node.left == node.right ? 1U : 0U);
      return locations[child].kind == OperandKind::Scratch && uses[child] == consumed;
    };
    if (!dies(leftId) && arity(node.op) == 2 && commutative(node.op) && dies(rightId)) {
      std::swap(leftId, rightId);
    }
    std::optional<unsigned> destination;
    if (dies(leftId)) {
      destination = static_cast<unsigned>(locations[leftId].value);
    } else {
      destination = allocate();
    }
    if (!destination) {
      return Result<NativePlan>::fail("scratch-register budget exceeded");
    }
    const Operand left = locations[leftId];
    if (left.kind != OperandKind::Scratch || left.value != *destination) {
      append(MachineOp::Mov, *destination, left);
    }
    append(machineOp(node.op), *destination, arity(node.op) == 2 ? locations[rightId] : Operand{});
    for (Id child : {node.left, arity(node.op) == 2 ? node.right : invalidId}) {
      if (child == invalidId) {
        continue;
      }
      if (--uses[child] == 0 && locations[child].kind == OperandKind::Scratch &&
          locations[child].value != *destination) {
        busy[locations[child].value] = false;
      }
    }
    locations[id] = {OperandKind::Scratch, *destination};
    if (plan.instructions.size() > options.maxInstructions) {
      return Result<NativePlan>::fail("instruction budget exceeded");
    }
  }
  auto root = locations[program.root];
  if (root.kind == OperandKind::Input) {
    auto destination = allocate();
    if (!destination) {
      return Result<NativePlan>::fail("root register budget exceeded");
    }
    append(MachineOp::Mov, *destination, root);
    root = {OperandKind::Scratch, *destination};
  }
  plan.resultRegister = static_cast<unsigned>(root.value);
  if (plan.instructions.size() > options.maxInstructions) {
    return Result<NativePlan>::fail("instruction budget exceeded");
  }
  if (auto reason = validate(plan); !reason.empty()) {
    return Result<NativePlan>::fail(reason);
  }
  return Result<NativePlan>::ok(std::move(plan));
}

std::string validate(const NativePlan &plan) {
  if (plan.width != Width::W32 && plan.width != Width::W64) {
    return "invalid native width";
  }
  if (!plan.scratchRegisters || plan.scratchRegisters > 6 ||
      plan.resultRegister >= plan.scratchRegisters || plan.instructions.empty() ||
      plan.instructions.size() > 1024) {
    return "invalid native shape";
  }
  std::array<bool, 6> initialized{};
  for (const auto &instruction : plan.instructions) {
    if (static_cast<unsigned>(instruction.op) > static_cast<unsigned>(MachineOp::Neg)) {
      return "invalid machine opcode";
    }
    if (instruction.destination >= plan.scratchRegisters) {
      return "bad destination";
    }
    if (instruction.op != MachineOp::Mov && !initialized[instruction.destination]) {
      return "uninitialized destination read";
    }
    if (!unaryMachine(instruction.op)) {
      switch (instruction.source.kind) {
      case OperandKind::Input:
        if (instruction.source.value > 1) {
          return "bad input";
        }
        break;
      case OperandKind::Scratch:
        if (instruction.source.value >= plan.scratchRegisters ||
            !initialized[instruction.source.value]) {
          return "uninitialized source";
        }
        break;
      case OperandKind::Immediate:
        if (instruction.op != MachineOp::Mov) {
          return "only MOV supports immediate operands in this backend";
        }
        if (instruction.source.value & ~widthMask(plan.width)) {
          return "immediate width mismatch";
        }
        break;
      default:
        return "invalid operand kind";
      }
    }
    initialized[instruction.destination] = true;
  }
  for (unsigned index = 0; index < plan.scratchRegisters; ++index) {
    if (!initialized[index]) {
      return "undefined asm output";
    }
  }
  return {};
}

Result<std::uint64_t> evaluate(const NativePlan &plan, std::uint64_t leftInput,
                               std::uint64_t rightInput) {
  if (auto reason = validate(plan); !reason.empty()) {
    return Result<std::uint64_t>::fail(reason);
  }
  std::array<std::uint64_t, 6> registers{};
  for (const auto &instruction : plan.instructions) {
    std::uint64_t source = 0;
    if (!unaryMachine(instruction.op)) {
      switch (instruction.source.kind) {
      case OperandKind::Input:
        source = instruction.source.value ? rightInput : leftInput;
        break;
      case OperandKind::Scratch:
        source = registers[instruction.source.value];
        break;
      case OperandKind::Immediate:
        source = instruction.source.value;
        break;
      }
    }
    auto &destination = registers[instruction.destination];
    switch (instruction.op) {
    case MachineOp::Mov:
      destination = source;
      break;
    case MachineOp::Add:
      destination += source;
      break;
    case MachineOp::Sub:
      destination -= source;
      break;
    case MachineOp::IMul:
      destination *= source;
      break;
    case MachineOp::And:
      destination &= source;
      break;
    case MachineOp::Or:
      destination |= source;
      break;
    case MachineOp::Xor:
      destination ^= source;
      break;
    case MachineOp::Not:
      destination = ~destination;
      break;
    case MachineOp::Neg:
      destination = UINT64_C(0) - destination;
      break;
    }
    destination &= widthMask(plan.width);
  }
  return Result<std::uint64_t>::ok(registers[plan.resultRegister]);
}

Result<InlineAssembly> renderLLVMAssembly(const NativePlan &plan) {
  if (auto reason = validate(plan); !reason.empty()) {
    return Result<InlineAssembly>::fail(reason);
  }
  std::ostringstream text, constraints;
  const auto suffix = plan.width == Width::W32 ? "l" : "q";
  const auto modifier = plan.width == Width::W32 ? "k" : "q";
  auto registerName = [&](unsigned index) {
    return "${" + std::to_string(index) + ':' + modifier + '}';
  };
  for (const auto &instruction : plan.instructions) {
    std::string mnemonic = machineOpName(instruction.op);
    if (instruction.op == MachineOp::Mov && instruction.source.kind == OperandKind::Immediate &&
        plan.width == Width::W64) {
      mnemonic = "movabs";
    }
    text << mnemonic << suffix << ' ';
    if (!unaryMachine(instruction.op)) {
      if (instruction.source.kind == OperandKind::Immediate) {
        text << "$$0x" << std::hex << instruction.source.value << std::dec;
      } else {
        text << registerName(
            static_cast<unsigned>(instruction.source.value) +
            (instruction.source.kind == OperandKind::Input ? plan.scratchRegisters : 0));
      }
      text << ", ";
    }
    text << registerName(instruction.destination) << "\n\t";
  }
  for (unsigned index = 0; index < plan.scratchRegisters; ++index) {
    constraints << "=&r,";
  }
  constraints << "r,r,~{flags}";
  return Result<InlineAssembly>::ok({text.str(), constraints.str()});
}

} // namespace a2mba::hybrid_core
