#include "a2mba/Modular.h"
#include "a2mba/Random.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <cstdlib>
#include <utility>

namespace {

[[noreturn]] void fail(llvm::StringRef message) {
  llvm::errs() << "a2mba-selftest: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

template <typename T> T unwrapOrExit(llvm::Expected<T> result, llvm::StringRef operation) {
  if (!result) {
    llvm::errs() << "a2mba-selftest: " << operation << ": ";
    llvm::logAllUnhandledErrors(result.takeError(), llvm::errs());
    std::exit(EXIT_FAILURE);
  }
  return std::move(*result);
}

void require(bool condition, llvm::StringRef message) {
  if (!condition)
    fail(message);
}

void testModularArithmetic() {
  const llvm::APInt value(32, 3);
  const llvm::APInt inverse = unwrapOrExit(a2mba::modularInverseOdd(value), "inverse of 3");

  require(inverse == llvm::APInt(32, 0xaaaaaaabU), "inverse of 3 modulo 2^32 changed");
  require(a2mba::isModularInverse(value, inverse),
          "modular inverse validation rejected a valid inverse");
  require(!a2mba::isModularInverse(value, llvm::APInt(32, 1)),
          "modular inverse validation accepted an invalid inverse");

  auto evenInverse = a2mba::modularInverseOdd(llvm::APInt(32, 2));
  require(!evenInverse, "an even value unexpectedly had an inverse modulo 2^32");
  llvm::consumeError(evenInverse.takeError());

  auto samples = a2mba::RandomSource::deterministic(0xc0dec0dec0dec0deULL);
  for (unsigned index = 0; index != 64; ++index) {
    const unsigned bitWidth = index % 2 == 0 ? 32 : 64;
    const std::uint64_t raw = unwrapOrExit(samples.next64(), "modular inverse sample");
    const llvm::APInt oddValue(bitWidth, raw | 1);
    const llvm::APInt oddInverse =
        unwrapOrExit(a2mba::modularInverseOdd(oddValue), "sample modular inverse");
    require(a2mba::isModularInverse(oddValue, oddInverse),
            "generated odd value failed modular inverse validation");
  }
}

void testDeterministicRandomness() {
  auto first = a2mba::RandomSource::deterministic(0x123456789abcdef0ULL);
  auto second = a2mba::RandomSource::deterministic(0x123456789abcdef0ULL);

  require(first.isDeterministic() && second.isDeterministic(),
          "deterministic random source did not report its mode");
  for (unsigned index = 0; index != 16; ++index) {
    const std::uint64_t firstValue = unwrapOrExit(first.next64(), "first deterministic sequence");
    const std::uint64_t secondValue =
        unwrapOrExit(second.next64(), "second deterministic sequence");
    require(firstValue == secondValue, "equal seeds produced different random sequences");
  }

  auto bounded = a2mba::RandomSource::deterministic(7);
  for (unsigned index = 0; index != 64; ++index) {
    const std::uint64_t value = unwrapOrExit(bounded.uniform(17), "uniform random value");
    require(value < 17, "uniform random value escaped its upper bound");
  }
  require(unwrapOrExit(bounded.uniform(1), "unit random range") == 0,
          "uniform(1) returned a non-zero value");
  require(!unwrapOrExit(bounded.chance(0), "zero-percent chance"), "zero-percent chance succeeded");
  require(unwrapOrExit(bounded.chance(100), "certain chance"), "100-percent chance failed");

  auto emptyRange = bounded.uniform(0);
  require(!emptyRange, "uniform(0) unexpectedly succeeded");
  llvm::consumeError(emptyRange.takeError());
}

} // namespace

int main() {
  testModularArithmetic();
  testDeterministicRandomness();
  return EXIT_SUCCESS;
}
