#include "a2mba/Statistics.h"

#include "llvm/Support/raw_ostream.h"

namespace a2mba {

void Statistics::recordSkip(SkipReason reason) {
  const unsigned index = static_cast<unsigned>(reason);
  if (reason != SkipReason::None && index < skipped.size()) {
    ++skipped[index];
  }
}

void Statistics::print(llvm::raw_ostream &output) const {
  output << "A2MBA LLVM 21\n\n"
         << "functions:\n"
         << "  visited: " << functionsVisited << '\n'
         << "  selected: " << functionsSelected << '\n'
         << "  transformed: " << functionsTransformed << "\n\n"
         << "instructions:\n"
         << "  visited: " << instructionsVisited << '\n'
         << "  candidates: " << candidates << '\n'
         << "  transformed: " << instructionsTransformed << "\n\n"
         << "transforms:\n"
         << "  rule explosion: " << ruleExplosions << '\n'
         << "  modular scale: " << modularScales << '\n'
         << "  context trap: " << contextTraps << '\n'
         << "  ADC: " << adcTransforms << '\n'
         << "  SBB: " << sbbTransforms << '\n'
         << "  paper RCR/RCL: " << paperRotates << "\n\n"
         << "skipped:\n";

  for (unsigned index = 1; index < skipped.size(); ++index) {
    if (skipped[index] == 0) {
      continue;
    }
    output << "  " << describe(static_cast<SkipReason>(index)) << ": " << skipped[index] << '\n';
  }
}

} // namespace a2mba
