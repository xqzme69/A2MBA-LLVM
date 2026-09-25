# Third-party notices

A²MBA-LLVM is distributed under the MIT license in `LICENSE`.

When Windows LLVM has plugins disabled, the standalone `a2mba-opt.exe` driver links selected LLVM 21 components. Other builds use the LLVM installation selected at configure time. The applicable LLVM 21.1.8 terms are reproduced in `LICENSES/LLVM.txt`.

A²MBA uses LLVM Support's regular-expression implementation. Its original Henry Spencer and University of California notices are reproduced without modification in `LICENSES/LLVM-regex.txt`.

SaMBA (*Increasing Mixed Boolean-Arithmetic Complexity Through Equality Saturation*) and asmMBA (*Robust Virtualization Obfuscation with Assembly-Based Mixed Boolean-Arithmetic*) informed parts of the design. This repository does not vendor their code, rules, datasets, or results.
