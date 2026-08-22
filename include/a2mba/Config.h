#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <optional>
#include <string>

namespace a2mba {

enum class ImplementationMode {
  Verified,
  Paper,
};

enum class ProtectionLevel {
  Light,
  Balanced,
  Medium,
  Heavy,
};

enum class FunctionSelectionKind {
  Annotated,
  All,
  Regex,
};

enum class TransformKind {
  Auto,
  RuleExplosion,
  ModularScale,
  ContextTrap,
  Adc,
  Sbb,
  PaperRcrRcl,
};

struct ProtectionProfile {
  unsigned minimumDepth;
  unsigned maximumDepth;
  unsigned candidateProbability;
  unsigned architecturalProbability;
  unsigned contextTrapProbability;
};

struct Config {
  ImplementationMode mode = ImplementationMode::Verified;
  ProtectionLevel level = ProtectionLevel::Balanced;
  FunctionSelectionKind functionSelection = FunctionSelectionKind::Annotated;
  TransformKind forcedTransform = TransformKind::Auto;
  std::optional<std::uint64_t> seed;
  std::optional<unsigned> forcedDepth;
  std::optional<unsigned> forcedProbability;
  std::string functionPattern;
  bool printStatistics = false;
  bool printDiagnostics = false;

  static llvm::Expected<Config> loadFromEnvironment();

  ProtectionProfile profile() const;
};

llvm::StringRef toString(ImplementationMode mode);
llvm::StringRef toString(ProtectionLevel level);
llvm::StringRef toString(TransformKind transform);

} // namespace a2mba
