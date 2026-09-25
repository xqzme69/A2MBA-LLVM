# A²MBA-LLVM

A²MBA-LLVM brings the architectural and anti-generalization transforms from *Unifying Mixed Boolean-Arithmetic Obfuscation by Architectural and Anti-Generalization Hardening* to LLVM 21.

It is a separate implementation, not a port of the authors' LLVM 15 prototype. Where their work leaves a parameter unspecified, the choice made here is documented in [Design](docs/DESIGN.md) or [Intentional deviations](docs/PAPER_DEVIATIONS.md).

## Scope

- LLVM 21.x only; LLVM 15-20 and 22 or newer are rejected.
- Linux x86-64 and Windows x86-64.
- Scalar `i32` and `i64` integer operations.
- Stock Clang/LLVM through a New-PM loadable plugin on Linux or the bundled `a2mba-opt` driver on Windows; no compiler fork.
- Normal compile and link flows. ThinLTO and Full LTO are not supported in v0.1.

The default is `verified`, `balanced`, and annotated functions only. The `paper` mode keeps the RCR/RCL comparison transform separate from that default.

The authors' resilience and overhead figures are for their LLVM 15 prototype, not this pass. Our older measurements are in [Benchmarking](docs/BENCHMARKING.md); they predate the current nonlinear hardening.

## Build

You need CMake 3.24+, a C++20 compiler, Python 3.9+, Z3, and LLVM 21 development files. The pinned Python dependencies include lit and Z3:

```bash
python -m pip install -r requirements-test.txt
cmake -S . -B build -DLLVM_DIR=/path/to/llvm-21/lib/cmake/llvm
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Build against the LLVM installation used by Clang; matching the major version alone is not enough for a pass plugin. Platform setup and common failures are in [Installation](docs/INSTALL.md).

On Linux, the loadable plugin marks functions `noredzone` when an AAMBA gadget preserves flags on the stack. The official portable Windows LLVM 21.1.8 package has `LLVM_ENABLE_PLUGINS=OFF`. With that package, CMake builds `a2mba-opt.exe` instead of a DLL, keeping the pass and driver in the same LLVM process.

For optimized Windows builds, the wrapper gets bitcode from Clang, runs `a2mba-opt.exe`, then sends the protected bitcode back to Clang for code generation with further LLVM passes disabled. [Installation](docs/INSTALL.md) covers the commands; [Intentional deviations](docs/PAPER_DEVIATIONS.md) explains why the Windows path differs.

## Protect a function

```c
#include <stdint.h>
#include <a2mba.h>

A2MBA_PROTECT_NOINLINE
uint64_t verify(uint64_t lhs, uint64_t rhs) {
    return (lhs ^ rhs) + UINT64_C(123);
}
```

From the source tree:

```bash
python tools/a2mba-clang.py --plugin /path/to/A2MBA.so \
  --level balanced source.c -O3 -o app
```

On Windows, run the installed `bin/a2mba-clang.py` through Python. It finds `bin/a2mba-opt.exe` beside it; use `--opt` or `A2MBA_OPT` for another location. `--plugin` and `A2MBA_PLUGIN` apply only on Linux.

Check the toolchain first:

```bash
python tools/a2mba-clang.py --doctor --clang clang-21 --plugin /path/to/A2MBA.so
```

`--doctor` checks Clang and runs a small compile through the actual pass path: the plugin on Linux, the standalone driver and bitcode-to-object stage on Windows. A passing check ends with `compatible: yes`.

## Wrapper options

```text
--mode verified|paper
--level light|balanced|medium|heavy
--hybrid off|ir|native
--hybrid-region none|stateful
--hybrid-layers none|profile|context-adc|context-sbb|context-random
--seed UINT64
--functions annotated|all|regex:<pattern>
--stats
--clang PATH
--opt PATH
--plugin PATH
--doctor
```

Other arguments keep their order for Clang. If a child process fails, its status becomes the wrapper's status. Linux loads the plugin directly; optimized Windows builds use Clang/`a2mba-opt`/Clang, with one translation unit per compile-only command. `--seed` fixes the transform choices; otherwise they use OS randomness.

The wrapper encodes its settings in `A2MBA_OPTIONS`, a semicolon-separated `key=value` string. The same interface works with `opt`:

```bash
A2MBA_OPTIONS='mode=verified;level=balanced;seed=1337;functions=all' \
  opt -load-pass-plugin=./A2MBA.so -passes=a2mba input.ll -S -o protected.ll
```

PowerShell:

```powershell
$env:A2MBA_OPTIONS = 'mode=verified;level=balanced;seed=1337;functions=all'
build\bin\a2mba-opt.exe -passes=a2mba input.ll -S -o protected.ll
```

## Modes and profiles

`verified` includes ADC, SBB, Rule Explosion, modular identity wrapping, and the self-contained Context Trap pair. It excludes RCR/RCL.

`paper` adds the RCR/RCL construction for comparison with the authors' work. It is not a port of their prototype.

`--hybrid ir` uses bounded equality saturation to replace a selected operation. `--hybrid native` lowers the chosen expression into a register-only x86-64 assembly block. Both still pass through the nonlinear envelope. `--hybrid-layers` adds Context Trap and ADC/SBB outside it; `none` turns off those outer layers only. Hybrid and layer options are off by default.

`--hybrid-region stateful` protects a straight-line chain as one unit. Each encoded value updates the state used by the next operation; only the region exit exposes a decoded result. It requires `--hybrid ir` or `--hybrid native` and is off by default. Resilience needs to be measured on whole regions, not isolated expressions.

The authors specify the depth ranges for `light`, `medium`, and `heavy`, but not these probabilities. `balanced` is also our preset:

| Profile | Depth | Candidate probability |
| --- | ---: | ---: |
| `light` | 2-4 | 35% |
| `balanced` | 3-6 | 55% |
| `medium` | 8-12 | 70% |
| `heavy` | 16-24 | 100% |

## Validation and measurement

Run the build, tests, and wrapper smoke check with:

```bash
python scripts/validate.py --llvm-dir /path/to/llvm-21/lib/cmake/llvm
```

For object-hash diversity and runtime measurements:

```bash
python scripts/diversity.py --plugin /path/to/A2MBA.so --variants 10 sample.c
python scripts/benchmark.py --plugin /path/to/A2MBA.so --iterations 10 sample.c
```

The commands measure the supplied source and toolchain, not a general protection score. See [Benchmarking](docs/BENCHMARKING.md) for how to interpret them.

The tests run hybrid and Context Trap output through a fresh `default<O3>` pipeline and check that input-dependent multiplication remains. Code-generation tests also check that ADC/SBB does not collapse to one carry setup, compensation order, or flag-handling pattern. Neither gate measures resistance to an external simplifier.

## Stateful-region snapshot

In the 2026-09-19 LLVM 21.1.8 run, all 100 `hybrid=ir;hybrid-region=stateful` expressions changed and passed the sampled equivalence check. None could be flattened within the 100,000-node export limit; even the smallest full expansion was estimated at 351,382,325,827 nodes.

The `(x ^ x) - y` regression case produced a 185-node stateful DAG. Full expansion was estimated at 258,677,935,894,761 nodes, and `default<O3>` left 48 input-dependent multiplications. This run predates operand stabilization for LLVM `undef`; it is a corpus/optimizer check on an older build, not a ProMBA, CoBRA, or GAMBA result. Details are in [Benchmarking](docs/BENCHMARKING.md).

## Historical Context Trap evaluation

In a separate 1,000-expression LLVM 21.1.8 run, each expression received one randomized Context Trap layer and passed an independent full-width equivalence check. ProMBA returned 870 deceptive simplifications, 130 top-level timeouts, and no correct simplifications. DSR was 100% of the 870 completed results; timeouts were excluded, as in the authors' definition.

![ProMBA outcomes for the randomized Context Trap corpus](docs/results/promba-outcomes.png)

That run predates the nonlinear envelope after Context Trap. [Benchmarking](docs/BENCHMARKING.md) has the full outcomes, CoBRA/GAMBA parser results, parameter check, configuration, and hashes. Its numbers do not describe the current pass.

## Historical hybrid baseline

The 2026-09-18 LLVM 21.1.8 ablation compared classic A²MBA, hybrid IR, native lowering, and hybrid IR with Context Trap plus ADC/SBB. Its old pure-hybrid output was linear MBA: GAMBA reduced all 82 changed IR expressions correctly. GAMBA mostly rejected the layered output at parsing, so that result says nothing about resilience.

The current hybrid and Context Trap paths add an input-dependent nonlinear envelope, and the architectural gadgets vary their instruction patterns. The old results remain in [Benchmarking](docs/BENCHMARKING.md). A newer Windows flat-expression check found that CoBRA still returned source-matching candidates for every exported changed hybrid IR and layered expression. It did not test recovery of a whole stateful region; the input and verification limits are recorded in [Benchmarking](docs/BENCHMARKING.md).

## Security boundary

A²MBA makes selected data-flow harder for automated tools to analyze. It does not make code unrecoverable or prevent its owner from tracing it, and it is not a substitute for control-flow integrity, encryption, or secret management. See [Security model](docs/SECURITY_MODEL.md).

## Documentation

- [Design](docs/DESIGN.md)
- [Installation](docs/INSTALL.md)
- [Transforms](docs/TRANSFORMS.md)
- [Paper mapping](docs/PAPER_MAPPING.md)
- [Intentional deviations](docs/PAPER_DEVIATIONS.md)
- [Correctness](docs/CORRECTNESS.md)
- [Security model](docs/SECURITY_MODEL.md)
- [Benchmarking](docs/BENCHMARKING.md)
- [Security policy](.github/SECURITY.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

The code is MIT-licensed. [CITATION.cff](CITATION.cff) credits the research separately; binary packages include the notices required by their linked LLVM components.
