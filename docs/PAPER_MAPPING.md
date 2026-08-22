# Paper mapping

This map links *Unifying Mixed Boolean-Arithmetic Obfuscation by Architectural and Anti-Generalization Hardening* to the corresponding project code. A²MBA-LLVM is not source-compatible with the unpublished LLVM 15 prototype.

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
| Section 5.4, overhead | `scripts/benchmark.py`, `docs/BENCHMARKING.md` | Provides a local measurement procedure; no aggregate A²MBA-LLVM overhead result is published here. |
| Section 5.5, diversity | `test/Determinism`, `scripts/diversity.py` | Checks same-seed determinism and different-seed object hashes for a supplied workload. |

## Architecture names

The project keeps the paper's two high-level names:

- AAMBA: architectural state-dependent identities, currently ADC/SBB plus the paper-only rotation example.
- AGT: Rule Explosion and the self-contained Context Trap pair.

`ModularScale` is a project utility derived from Rule Explosion's scaling construction, not a separately named paper transform.

## Evaluation boundary

The paper reports an LLVM 15 prototype, seven benchmark programs, deobfuscator success and deception rates, overhead factors, and 1,000 variants per benchmark. A²MBA-LLVM targets LLVM 21 and includes neither that prototype nor its complete experiment, so those results do not transfer to this implementation. The separate Context Trap snapshot in [BENCHMARKING.md](BENCHMARKING.md) uses a different corpus and scope.

[PAPER_DEVIATIONS.md](PAPER_DEVIATIONS.md) explains each deliberate difference. [BENCHMARKING.md](BENCHMARKING.md) describes the local measurement tools and their limits.
