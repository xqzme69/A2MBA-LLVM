#include "a2mba/Config.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Errc.h"

#include <cstdlib>
#include <limits>
#include <utility>

namespace a2mba {
namespace {

llvm::Expected<bool> parseBoolean(llvm::StringRef value, llvm::StringRef optionName) {
  if (value.equals_insensitive("true") || value == "1" || value.equals_insensitive("yes")) {
    return true;
  }
  if (value.equals_insensitive("false") || value == "0" || value.equals_insensitive("no")) {
    return false;
  }
  return llvm::createStringError(llvm::errc::invalid_argument, "%s expects true or false",
                                 optionName.str().c_str());
}

llvm::Error invalidValue(llvm::StringRef key, llvm::StringRef value) {
  return llvm::createStringError(llvm::errc::invalid_argument, "invalid A2MBA option %s=%s",
                                 key.str().c_str(), value.str().c_str());
}

llvm::Expected<unsigned> parseUnsigned(llvm::StringRef key, llvm::StringRef value,
                                       unsigned maximum) {
  unsigned parsed = 0;
  if (value.getAsInteger(10, parsed) || parsed > maximum) {
    return invalidValue(key, value);
  }
  return parsed;
}

llvm::Expected<std::uint64_t> parseSeed(llvm::StringRef value) {
  std::uint64_t parsed = 0;
  if (value.getAsInteger(0, parsed)) {
    return invalidValue("seed", value);
  }
  return parsed;
}

llvm::Expected<TransformKind> parseTransform(llvm::StringRef value) {
  if (value == "auto")
    return TransformKind::Auto;
  if (value == "rule-explosion")
    return TransformKind::RuleExplosion;
  if (value == "modular-scale")
    return TransformKind::ModularScale;
  if (value == "context-trap")
    return TransformKind::ContextTrap;
  if (value == "adc")
    return TransformKind::Adc;
  if (value == "sbb")
    return TransformKind::Sbb;
  if (value == "paper-rcr-rcl")
    return TransformKind::PaperRcrRcl;
  return invalidValue("transform", value);
}

} // namespace

llvm::Expected<Config> Config::loadFromEnvironment() {
  Config config;
  const char *rawEnvironment = std::getenv("A2MBA_OPTIONS");
  if (!rawEnvironment || *rawEnvironment == '\0') {
    return config;
  }

  llvm::SmallVector<llvm::StringRef, 16> entries;
  llvm::StringRef(rawEnvironment).split(entries, ';', -1, false);

  for (llvm::StringRef entry : entries) {
    entry = entry.trim();
    if (entry.empty()) {
      continue;
    }

    const auto [rawKey, rawValue] = entry.split('=');
    const llvm::StringRef key = rawKey.trim();
    llvm::StringRef value = rawValue.trim();
    if (key.empty() || value.empty()) {
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "A2MBA options use key=value syntax");
    }

    if (key == "mode") {
      if (value == "verified")
        config.mode = ImplementationMode::Verified;
      else if (value == "paper")
        config.mode = ImplementationMode::Paper;
      else
        return invalidValue(key, value);
      continue;
    }

    if (key == "level") {
      if (value == "light")
        config.level = ProtectionLevel::Light;
      else if (value == "balanced")
        config.level = ProtectionLevel::Balanced;
      else if (value == "medium")
        config.level = ProtectionLevel::Medium;
      else if (value == "heavy")
        config.level = ProtectionLevel::Heavy;
      else
        return invalidValue(key, value);
      continue;
    }

    if (key == "functions") {
      if (value == "annotated") {
        config.functionSelection = FunctionSelectionKind::Annotated;
        config.functionPattern.clear();
      } else if (value == "all") {
        config.functionSelection = FunctionSelectionKind::All;
        config.functionPattern.clear();
      } else if (value.consume_front("regex:") && !value.empty()) {
        config.functionSelection = FunctionSelectionKind::Regex;
        config.functionPattern = value.str();
      } else {
        return invalidValue(key, rawValue.trim());
      }
      continue;
    }

    if (key == "transform") {
      auto transform = parseTransform(value);
      if (!transform)
        return transform.takeError();
      config.forcedTransform = *transform;
      continue;
    }

    if (key == "seed") {
      auto seed = parseSeed(value);
      if (!seed)
        return seed.takeError();
      config.seed = *seed;
      continue;
    }

    if (key == "depth") {
      auto depth = parseUnsigned(key, value, 64);
      if (!depth)
        return depth.takeError();
      if (*depth == 0)
        return invalidValue(key, value);
      config.forcedDepth = *depth;
      continue;
    }

    if (key == "probability") {
      auto probability = parseUnsigned(key, value, 100);
      if (!probability)
        return probability.takeError();
      config.forcedProbability = *probability;
      continue;
    }

    if (key == "stats") {
      auto enabled = parseBoolean(value, key);
      if (!enabled)
        return enabled.takeError();
      config.printStatistics = *enabled;
      continue;
    }

    if (key == "diagnostics") {
      auto enabled = parseBoolean(value, key);
      if (!enabled)
        return enabled.takeError();
      config.printDiagnostics = *enabled;
      continue;
    }

    return llvm::createStringError(llvm::errc::invalid_argument, "unknown A2MBA option: %s",
                                   key.str().c_str());
  }

  if (config.forcedTransform == TransformKind::PaperRcrRcl &&
      config.mode != ImplementationMode::Paper) {
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "paper-rcr-rcl is available only with mode=paper");
  }

  return config;
}

ProtectionProfile Config::profile() const {
  ProtectionProfile selected{};
  switch (level) {
  case ProtectionLevel::Light:
    selected = {2, 4, 35, 20, 15};
    break;
  case ProtectionLevel::Balanced:
    selected = {3, 6, 55, 20, 20};
    break;
  case ProtectionLevel::Medium:
    selected = {8, 12, 70, 30, 25};
    break;
  case ProtectionLevel::Heavy:
    selected = {16, 24, 100, 35, 30};
    break;
  }

  if (forcedDepth) {
    selected.minimumDepth = *forcedDepth;
    selected.maximumDepth = *forcedDepth;
  }
  if (forcedProbability) {
    selected.candidateProbability = *forcedProbability;
  }
  return selected;
}

llvm::StringRef toString(ImplementationMode mode) {
  switch (mode) {
  case ImplementationMode::Verified:
    return "verified";
  case ImplementationMode::Paper:
    return "paper";
  }
  return "unknown";
}

llvm::StringRef toString(ProtectionLevel level) {
  switch (level) {
  case ProtectionLevel::Light:
    return "light";
  case ProtectionLevel::Balanced:
    return "balanced";
  case ProtectionLevel::Medium:
    return "medium";
  case ProtectionLevel::Heavy:
    return "heavy";
  }
  return "unknown";
}

llvm::StringRef toString(TransformKind transform) {
  switch (transform) {
  case TransformKind::Auto:
    return "auto";
  case TransformKind::RuleExplosion:
    return "rule-explosion";
  case TransformKind::ModularScale:
    return "modular-scale";
  case TransformKind::ContextTrap:
    return "context-trap";
  case TransformKind::Adc:
    return "adc";
  case TransformKind::Sbb:
    return "sbb";
  case TransformKind::PaperRcrRcl:
    return "paper-rcr-rcl";
  }
  return "unknown";
}

} // namespace a2mba
