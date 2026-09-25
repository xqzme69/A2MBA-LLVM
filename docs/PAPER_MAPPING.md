# Paper mapping

Where the ideas from *Unifying Mixed Boolean-Arithmetic Obfuscation by Architectural and Anti-Generalization Hardening* appear in this repository. This is an LLVM 21 implementation, not a port of the authors' LLVM 15 code.

| Paper material | Project location | Status in this implementation |
| --- | --- | --- |
| Section 3.1, AAMBA | `lib/AAMBA.cpp`, `include/a2mba/AAMBA.h` | x86-64 architectural identities implemented for `i32` and `i64`. |
| Equation (3), `ADC(x,C)-C-1` | `applyArchitecturalIdentity(..., Adc)` | Available in `verified` and `paper` modes through the atomic ADC primitive. |
| Section 3.1, SBB family | `applyArchitecturalIdentity(..., Sbb)` | Implements the corresponding `SBB(x,C)+C+1` identity with `CF=1`. |
| Equation (4), RCR/RCL | `applyArchitecturalIdentity(..., PaperRcrRcl)` | `paper` mode only. The pair is inverse for either initial carry, so it is not used as a verified carry-dependence argument. |
| Section 3.2 and Equation (5), context trap | `applyContextTrap()` in `lib/AGT.cpp` | Builds a masked trap whose local precondition is true by construction. |
| Equation (6), fallacious generalized rule | `applyContextTrap()` in `lib/AGT.cpp` | Pairs the trap with a forced-sign trigger and compensating reconstruction that preserves the source value for every input. |
| Equation (7), Rule Explosion | `applyRuleExplosion()` in `lib/AGT.cpp` | Addition formula implemented with module-unique odd constants and `APInt` inverses. |
| Equation (8), constant-specific learned rule | Rule Explosion parameterization | Motivates unique constants. A project-specific Context Trap snapshot is documented, but no ProMBA corpus is shipped in this repository. |
| Algorithm 1, probabilistic dispatcher | `lib/A2MBA.cpp`, `Config::profile()` | Implemented by a module driver with function-local worklists and conservative eligibility. |
| Section 4.1, LLVM traversal | `lib/Plugin.cpp`, `lib/A2MBA.cpp` | Modernized from an LLVM 15 Function Pass to an LLVM 21 New-PM module pass. |
| Section 4.2, EFLAGS handling | `lib/AAMBA.cpp` | State-sensitive work is emitted atomically with explicit side effects; affected SysV functions disable the red zone. |
| Section 4.3, CSPRNG and unique constants | `lib/Random.cpp`, `lib/Modular.cpp`, `lib/Context.cpp` | OS randomness by default, deterministic stream with `seed`, module-wide uniqueness, and `APInt` modular arithmetic. |
| Section 5.2, differential correctness evaluation | `test/Runtime`, `scripts/validate.py` | Local gates exist; the paper's 7-program, 100-variant, 10,000-input result is not attributed to this project. |
| Section 5.4, overhead | `scripts/benchmark.py`, `scripts/baseline.py`, `docs/BENCHMARKING.md` | Local measurement commands and a separate hybrid run. |
| Section 5.5, diversity | `test/Determinism`, `scripts/diversity.py` | Checks same-seed determinism and different-seed object hashes for a supplied workload. |

The hybrid e-graph, native register lowering, nonlinear envelope, and stateful regions were added here. SaMBA and asmMBA informed the first two; their implementations and results are not reused.

## Architecture names

The two names from the authors' work are:

- AAMBA: architectural state-dependent identities, currently ADC/SBB plus the paper-only rotation example.
- AGT: Rule Explosion and the self-contained Context Trap pair.

`ModularScale` is our helper built from Rule Explosion's scaling identity, not a named transform in their work.

## Evaluation boundary

The authors' results come from an LLVM 15 prototype, seven benchmark programs, and 1,000 variants per benchmark. This repository does not contain that prototype or reproduce its full experiment. Its deobfuscator rates and overhead factors cannot be assigned to this LLVM 21 pass. Our Context Trap and hybrid runs in [Benchmarking](BENCHMARKING.md) use different inputs and methods.

Implementation differences are in [Intentional deviations](PAPER_DEVIATIONS.md); local measurements and their limits are in [Benchmarking](BENCHMARKING.md).
