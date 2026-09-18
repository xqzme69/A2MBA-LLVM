#include "Internal.h"
#include "a2mba/HybridCore.h"

#include <cstdlib>
#include <iostream>
#include <set>

using namespace a2mba::hybrid_core;
#define CHECK(condition)                                                                           \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n";                            \
      std::exit(1);                                                                                \
    }                                                                                              \
  } while (false)
namespace {
std::uint64_t evaluatedCases = 0;

std::uint64_t referenceResult(Op operation, std::uint64_t leftInput, std::uint64_t rightInput,
                              Width width) {
  auto seedProgram = makeSeed(operation, width);
  CHECK(seedProgram);
  auto result = evaluate(*seedProgram.value, leftInput, rightInput);
  CHECK(result);
  return *result.value;
}

void validity() {
  CHECK(!makeSeed(Op::Input, Width::W64));
  CHECK(!makeSeed(Op::Add, static_cast<Width>(8)));
  auto invalidProgram = *makeSeed(Op::Add, Width::W32).value;
  invalidProgram.nodes[2].left = 2;
  CHECK(!validate(invalidProgram).empty());
  invalidProgram.nodes[2].left = 0;
  invalidProgram.nodes[0].payload = 2;
  CHECK(!validate(invalidProgram).empty());
  invalidProgram.nodes[0] = {Op::Constant, invalidId, invalidId, UINT64_MAX};
  CHECK(!validate(invalidProgram).empty());

  const auto seedProgram = *makeSeed(Op::Add, Width::W64).value;
  SearchOptions searchOptions;
  searchOptions.maxDepth = 0;
  CHECK(!search(seedProgram, searchOptions));
  searchOptions = {};
  searchOptions.minimumAstNodes = searchOptions.maxAstNodes + 1;
  CHECK(!search(seedProgram, searchOptions));
  searchOptions = {};
  searchOptions.maxEGraphNodes = 3;
  searchOptions.minimumAstNodes = 4;
  CHECK(!search(seedProgram, searchOptions));
  searchOptions.minimumAstNodes = 3;
  auto budgetLimitedSearch = search(seedProgram, searchOptions);
  CHECK(budgetLimitedSearch);
  CHECK(budgetLimitedSearch.value->report.nodeBudgetHit);
  searchOptions = {};
  searchOptions.maxMatches = 1;
  searchOptions.maxMatchSteps = 1;
  CHECK(search(seedProgram, searchOptions));

  NativeOptions nativeOptions;
  nativeOptions.maxInstructions = 1;
  CHECK(!prepareNative(seedProgram, {}, nativeOptions));
}

void congruenceAndMatching() {
  detail::EGraph graph(Width::W64, 32);
  const Id leftInput = *graph.add({Op::Input, invalidId, invalidId, 0});
  const Id rightInput = *graph.add({Op::Input, invalidId, invalidId, 1});
  const Id firstConstant = *graph.add({Op::Constant, invalidId, invalidId, 7});
  const Id secondConstant = *graph.add({Op::Constant, invalidId, invalidId, 11});
  const Id inputSum = *graph.add({Op::Add, leftInput, rightInput, 0});
  const Id constantSum = *graph.add({Op::Add, firstConstant, secondConstant, 0});
  graph.unite(leftInput, firstConstant);
  graph.unite(rightInput, secondConstant);
  graph.rebuild();
  CHECK(graph.find(inputSum) == graph.find(constantSum));
  CHECK(graph.checkInvariants().empty());

  detail::Pattern repeated{{{Op::Input, invalidId, invalidId, 0}, {Op::Add, 0, 0, 0}}, 1};
  detail::MatchBudget budget{0, 1000, 16};
  CHECK(detail::match(graph, repeated, inputSum, budget).empty());
  graph.unite(leftInput, rightInput);
  graph.rebuild();
  budget = {0, 1000, 16};
  CHECK(!detail::match(graph, repeated, inputSum, budget).empty());
  CHECK(graph.checkInvariants().empty());
}

void cyclicExtraction() {
  detail::EGraph graph(Width::W64, 32);
  const Id input = *graph.add({Op::Input, invalidId, invalidId, 0});
  const Id zero = *graph.add({Op::Constant, invalidId, invalidId, 0});
  const Id sum = *graph.add({Op::Add, input, zero, 0});
  graph.unite(input, sum);
  graph.rebuild();

  SearchOptions searchOptions;
  searchOptions.maxDepth = 6;
  searchOptions.maxAstNodes = 31;
  auto candidates = detail::extract(graph, input, searchOptions);
  CHECK(!candidates.empty());
  for (const auto &candidate : candidates) {
    CHECK(candidate.metrics.depth <= 6 && candidate.metrics.astNodes <= 31);
    auto answer = evaluate(candidate.program, UINT64_MAX, 123);
    CHECK(answer && *answer.value == UINT64_MAX);
    ++evaluatedCases;
  }
}

void nativeCorners() {
  Program program{Width::W64,
                  {{Op::Input, invalidId, invalidId, 0},
                   {Op::Constant, invalidId, invalidId, UINT64_C(0xfedcba9876543210)},
                   {Op::Sub, 0, 1, 0},
                   {Op::Mul, 2, 2, 0}},
                  3};
  auto nativePlan = lowerNative(program);
  CHECK(nativePlan);
  CHECK(renderLLVMAssembly(*nativePlan.value).value->text.find("movabsq") != std::string::npos);
  for (std::uint64_t input : {UINT64_C(0), UINT64_C(1), UINT64_MAX}) {
    CHECK(evaluate(program, input, 0).value == evaluate(*nativePlan.value, input, 0).value);
    ++evaluatedCases;
  }

  auto invalidPlan = *nativePlan.value;
  invalidPlan.instructions[0].source = {OperandKind::Scratch, 5};
  CHECK(!validate(invalidPlan).empty());

  Program inputProgram{Width::W32, {{Op::Input, invalidId, invalidId, 1}}, 0};
  auto inputPlan = lowerNative(inputProgram);
  CHECK(inputPlan);
  CHECK(evaluate(*inputPlan.value, 3, UINT64_MAX).value == UINT64_C(0xffffffff));

  program.nodes.push_back({Op::Neg, 3, invalidId, 0});
  program.root = 4;
  auto negationPlan = lowerNative(program);
  CHECK(negationPlan);
  CHECK(evaluate(program, 17, 0).value == evaluate(*negationPlan.value, 17, 0).value);
}

void integrationProfiles() {
  constexpr std::uint64_t irI32SearchSeed = UINT64_C(13566731111258911605);
  constexpr std::uint64_t irI64SearchSeed = UINT64_C(17635233256074500550);
  constexpr std::uint64_t nativeI32SearchSeed = UINT64_C(7931773194558452508);
  constexpr std::uint64_t nativeI64SearchSeed = UINT64_C(14830410665993374091);

  auto mediumSearchOptions = [](std::uint64_t seed) {
    SearchOptions options;
    options.seed = seed;
    options.rounds = 4;
    options.maxEGraphNodes = 256;
    options.maxMatches = 768;
    options.maxMatchSteps = 200000;
    options.maxDepth = 8;
    options.minimumAstNodes = 15;
    options.maxAstNodes = 95;
    options.beamWidth = 6;
    return options;
  };

  auto checkIR = [&](Width width, std::uint64_t searchSeed) {
    const auto seedProgram = *makeSeed(Op::Add, width).value;
    const auto searchOptions = mediumSearchOptions(searchSeed);
    auto searchResult = search(seedProgram, searchOptions);
    CHECK(searchResult);
    CHECK(searchResult.value->candidates.front().metrics.astNodes >= searchOptions.minimumAstNodes);
  };
  checkIR(Width::W32, irI32SearchSeed);
  checkIR(Width::W64, irI64SearchSeed);

  NativeOptions mediumNative;
  mediumNative.maxScratchRegisters = 6;
  mediumNative.maxInstructions = 160;
  auto checkNative = [&](Width width, std::uint64_t searchSeed) {
    const auto seedProgram = *makeSeed(Op::Add, width).value;
    const auto searchOptions = mediumSearchOptions(searchSeed);
    auto searchResult = prepareNative(seedProgram, searchOptions, mediumNative);
    CHECK(searchResult);
    CHECK(searchResult.value->candidate.metrics.astNodes >= searchOptions.minimumAstNodes);
    CHECK(searchResult.value->native.scratchRegisters <= mediumNative.maxScratchRegisters);
    CHECK(searchResult.value->native.instructions.size() <= mediumNative.maxInstructions);
  };
  checkNative(Width::W32, nativeI32SearchSeed);
  checkNative(Width::W64, nativeI64SearchSeed);
}

void differential() {
  detail::Random random(0x728731);
  for (Width width : {Width::W32, Width::W64}) {
    for (Op operation : {Op::Add, Op::Sub, Op::Mul, Op::And, Op::Or, Op::Xor}) {
      const auto seedProgram = *makeSeed(operation, width).value;
      for (unsigned seed = 1; seed <= 8; ++seed) {
        SearchOptions options;
        options.seed = seed;
        auto nativeResult = prepareNative(seedProgram, options);
        CHECK(nativeResult);
        auto duplicate = prepareNative(seedProgram, options);
        CHECK(duplicate);
        CHECK(nativeResult.value->candidate.program == duplicate.value->candidate.program);
        CHECK(nativeResult.value->report.createdNodes <= options.maxEGraphNodes);
        CHECK(nativeResult.value->native.scratchRegisters <= 4);
        auto searchResult = search(seedProgram, options);
        CHECK(searchResult);
        for (const auto &candidate : searchResult.value->candidates) {
          CHECK(validate(candidate.program).empty());
          const std::uint64_t edgeCases[]{0,
                                          1,
                                          2,
                                          UINT64_MAX,
                                          UINT64_C(0x80000000),
                                          UINT64_C(0x7fffffff),
                                          UINT64_C(0x8000000000000000),
                                          UINT64_C(0x7fffffffffffffff)};
          for (auto leftInput : edgeCases) {
            for (auto rightInput : edgeCases) {
              CHECK(evaluate(candidate.program, leftInput, rightInput).value ==
                    referenceResult(operation, leftInput, rightInput, width));
              ++evaluatedCases;
            }
          }
        }
        for (unsigned test = 0; test < 512; ++test) {
          const auto leftInput = random.next();
          const auto rightInput = random.next();
          const auto expected = referenceResult(operation, leftInput, rightInput, width);
          CHECK(evaluate(nativeResult.value->candidate.program, leftInput, rightInput).value ==
                expected);
          CHECK(evaluate(nativeResult.value->native, leftInput, rightInput).value == expected);
          ++evaluatedCases;
        }
      }
    }
  }

  std::set<std::string> shapes;
  for (unsigned seed = 1; seed <= 12; ++seed) {
    SearchOptions options;
    options.seed = seed;
    auto nativeResult = prepareNative(*makeSeed(Op::Add, Width::W64).value, options);
    CHECK(nativeResult);
    shapes.insert(format(nativeResult.value->candidate.program));
  }
  CHECK(shapes.size() > 1);
  std::cout << "Distinct hybrid add/i64 shapes across 12 seeds: " << shapes.size() << '\n';
}
} // namespace

int main() {
  validity();
  congruenceAndMatching();
  cyclicExtraction();
  nativeCorners();
  integrationProfiles();
  differential();
  std::cout << "PASS: " << evaluatedCases
            << " differential/cycle checks plus structural/negative regressions\n";
}
