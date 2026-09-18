#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace a2mba::hybrid_core {

using Id = std::uint32_t;
inline constexpr Id invalidId = UINT32_MAX;

enum class Width : unsigned {
  W32 = 32,
  W64 = 64,
};

enum class Op : unsigned {
  Input,
  Constant,
  Add,
  Sub,
  Mul,
  And,
  Or,
  Xor,
  Not,
  Neg,
};

template <typename T> struct Result {
  std::optional<T> value;
  std::string error;

  explicit operator bool() const noexcept { return value.has_value(); }
  static Result ok(T result) { return {std::move(result), {}}; }
  static Result fail(std::string reason) { return {std::nullopt, std::move(reason)}; }
};

struct Node {
  Op op = Op::Input;
  Id left = invalidId;
  Id right = invalidId;
  std::uint64_t payload = 0;
  bool operator==(const Node &) const = default;
};

struct Program {
  Width width = Width::W64;
  std::vector<Node> nodes;
  Id root = invalidId;
  bool operator==(const Program &) const = default;
};

unsigned arity(Op op) noexcept;
const char *opName(Op op) noexcept;
std::uint64_t widthMask(Width width) noexcept;
std::string validate(const Program &program);
Result<Program> makeSeed(Op op, Width width);
Result<std::uint64_t> evaluate(const Program &program, std::uint64_t leftInput,
                               std::uint64_t rightInput);
std::string format(const Program &program);

struct SearchOptions {
  std::uint64_t seed = 1;
  unsigned rounds = 3;
  unsigned maxEGraphNodes = 192;
  unsigned maxMatches = 384;
  unsigned maxMatchSteps = 50000;
  unsigned maxDepth = 7;
  unsigned minimumAstNodes = 3;
  unsigned maxAstNodes = 63;
  unsigned beamWidth = 5;
  bool enableNegationRules = true;
};

struct Metrics {
  unsigned astNodes = 0;
  unsigned dagNodes = 0;
  unsigned depth = 0;
  unsigned alternations = 0;
  unsigned operatorKinds = 0;
  unsigned estimatedCost = 0;
  unsigned trivialPatterns = 0;
};

struct SearchReport {
  unsigned rounds = 0;
  unsigned createdNodes = 0;
  unsigned liveClasses = 0;
  unsigned matches = 0;
  unsigned matchSteps = 0;
  unsigned applications = 0;
  bool nodeBudgetHit = false;
  bool matchBudgetHit = false;
  bool fixedPointReached = false;
  std::vector<std::string> rulesUsed;
};

struct Candidate {
  Program program;
  Metrics metrics;
  std::int64_t proxyScore = 0;
};

struct SearchResult {
  std::vector<Candidate> candidates;
  SearchReport report;
};
Result<SearchResult> search(const Program &seed, const SearchOptions &options = {});
const char *rulesFingerprint() noexcept;

enum class MachineOp : unsigned {
  Mov,
  Add,
  Sub,
  IMul,
  And,
  Or,
  Xor,
  Not,
  Neg,
};

enum class OperandKind : unsigned {
  Input,
  Scratch,
  Immediate,
};

struct Operand {
  OperandKind kind = OperandKind::Input;
  std::uint64_t value = 0;
  bool operator==(const Operand &) const = default;
};

struct MachineInstruction {
  MachineOp op = MachineOp::Mov;
  unsigned destination = 0;
  Operand source;
};

struct NativeOptions {
  unsigned maxScratchRegisters = 4;
  unsigned maxInstructions = 96;
};

struct NativePlan {
  Width width = Width::W64;
  unsigned scratchRegisters = 0;
  unsigned resultRegister = 0;
  std::vector<MachineInstruction> instructions;
};
Result<NativePlan> lowerNative(const Program &program, const NativeOptions &options = {});
std::string validate(const NativePlan &plan);
Result<std::uint64_t> evaluate(const NativePlan &plan, std::uint64_t leftInput,
                               std::uint64_t rightInput);
const char *machineOpName(MachineOp op) noexcept;

struct InlineAssembly {
  std::string text;
  std::string constraints;
};
Result<InlineAssembly> renderLLVMAssembly(const NativePlan &plan);

struct Prepared {
  Candidate candidate;
  NativePlan native;
  SearchReport report;
  unsigned rejectedByNativeBudget = 0;
};

Result<Prepared> prepareNative(const Program &seed, const SearchOptions &searchOptions = {},
                               const NativeOptions &nativeOptions = {});

} // namespace a2mba::hybrid_core
