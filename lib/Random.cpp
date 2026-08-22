#include "a2mba/Random.h"

#include "llvm/Support/Errc.h"

#include <cstddef>
#include <cstdint>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>

#include <bcrypt.h>
#elif defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#else
#error "A2MBA v0.1 supports only Windows and Linux"
#endif

namespace a2mba {
namespace {

std::uint64_t nextSplitMix64(std::uint64_t &state) {
  state += 0x9e3779b97f4a7c15ULL;
  std::uint64_t value = state;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

llvm::Expected<std::uint64_t> readOperatingSystemRandom() {
  std::uint64_t value = 0;

#if defined(_WIN32)
  const NTSTATUS status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if (!BCRYPT_SUCCESS(status)) {
    return llvm::createStringError(llvm::errc::io_error,
                                   "BCryptGenRandom failed with NTSTATUS 0x%08x",
                                   static_cast<unsigned>(status));
  }
#elif defined(__linux__)
  auto *output = reinterpret_cast<unsigned char *>(&value);
  std::size_t remaining = sizeof(value);
  while (remaining != 0) {
    const ssize_t received = getrandom(output, remaining, 0);
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      return llvm::createStringError(std::error_code(errno, std::generic_category()),
                                     "getrandom failed");
    }
    output += received;
    remaining -= static_cast<std::size_t>(received);
  }
#endif

  return value;
}

} // namespace

RandomSource RandomSource::cryptographic() { return RandomSource(std::nullopt); }

RandomSource RandomSource::deterministic(std::uint64_t seed) { return RandomSource(seed); }

llvm::Expected<std::uint64_t> RandomSource::next64() {
  if (deterministicState) {
    return nextSplitMix64(*deterministicState);
  }
  return readOperatingSystemRandom();
}

llvm::Expected<std::uint64_t> RandomSource::uniform(std::uint64_t upperExclusive) {
  if (upperExclusive == 0) {
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "random upper bound must be non-zero");
  }

  const std::uint64_t rejectionThreshold = (std::uint64_t{0} - upperExclusive) % upperExclusive;
  while (true) {
    auto sample = next64();
    if (!sample) {
      return sample.takeError();
    }
    if (*sample >= rejectionThreshold) {
      return *sample % upperExclusive;
    }
  }
}

llvm::Expected<bool> RandomSource::chance(unsigned percentage) {
  if (percentage > 100) {
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "probability must be between 0 and 100");
  }
  if (percentage == 0) {
    return false;
  }
  if (percentage == 100) {
    return true;
  }

  auto sample = uniform(100);
  if (!sample) {
    return sample.takeError();
  }
  return *sample < percentage;
}

} // namespace a2mba
