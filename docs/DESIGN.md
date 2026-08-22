# Design

## Pipeline placement

A²MBA integrates with LLVM 21's New Pass Manager. `llvmGetPassPluginInfo()` registers the module pipeline name `a2mba` for explicit `opt` runs and an optimizer-last extension for Clang. The compile order is:

```text
C or C++
  -> normal Clang optimization
  -> A²MBA module pass
  -> LLVM backend
  -> object or executable
```

The placement is deliberate. InstCombine, GVN, SCCP, and related passes run before A²MBA, so the normal optimization pipeline does not immediately undo the inserted identities. `opt -passes=a2mba` remains available for tests, IR inspection, and experiments.

ThinLTO and Full LTO are outside the v0.1 contract. The plugin skips those phases rather than promising an optimizer-last position that has not been validated for LTO pipelines.

The official Windows package links the compatibility plugin against a separate static LLVM copy. Its wrapper therefore realizes the same order through a serialization boundary:

```text
C or C++
  -> normal Clang optimization and bitcode
  -> opt with A²MBA
  -> serialized protected bitcode
  -> Clang backend with LLVM passes disabled
  -> object or executable
```

This keeps plugin-owned in-memory LLVM objects out of the backend without giving post-A²MBA optimization another chance to remove the transforms.

## Module driver, function-local changes

The paper reports a Function Pass. Here, `A2MBAPass` is a module driver that applies function-local rewrites. Module scope keeps these objects under one lifetime:

- parsed configuration;
- deterministic or OS-backed randomness;
- the module-wide set of constants already used by Rule Explosion;
- accumulated statistics;
- processed/generated metadata.

The transform planner does not depend on host analysis-manager results. In particular, Context Trap creates its safe trap context with masks instead of asking ScalarEvolution to infer one. This also avoids crossing the official Windows package's static-LLVM DLL boundary with analysis keys owned by the host `opt` or Clang process.

The driver first snapshots eligible source instructions, then transforms that worklist. Generated instructions carry `!a2mba.generated`, and processed functions/modules are marked. A generated instruction is never reconsidered in the same pass invocation, and an already processed module is not expanded again by a second `a2mba` run.

## Function selection

Function selection is opt-in by default:

| Value | Behavior |
| --- | --- |
| `functions=annotated` | Select functions carrying the `a2mba` source annotation. This is the default. |
| `functions=all` | Select every otherwise eligible function. |
| `functions=regex:<pattern>` | Select matching LLVM function names. |

`sdk/a2mba.h` defines `A2MBA_PROTECT`, `A2MBA_PROTECT_NOINLINE`, and `A2MBA_IGNORE`. Use `A2MBA_PROTECT_NOINLINE` when the function must remain an identifiable boundary after normal optimization. An annotation only selects code during compilation; it adds no runtime dependency.

## Eligibility

The common eligibility layer accepts scalar `i32` and `i64` `add`, `sub`, `mul`, `and`, `or`, and `xor` instructions. It rejects, among other cases:

- generated or dead instructions;
- functions that already contain user inline assembly;
- other integer widths and vectors;
- floating-point, pointer, memory, call, and control-flow operations;
- instructions with `nsw` or `nuw` poison-generating flags;
- a transform that cannot preserve the selected opcode.

These skips are intentional. Removing `nsw` or `nuw` to force a modulo identity would change LLVM semantics, so v0.1 leaves the instruction untouched.

## Configuration

The plugin reads `A2MBA_OPTIONS` once per module. Entries use `key=value` syntax and semicolons as separators:

```text
mode=verified;level=balanced;seed=1337;functions=annotated;stats=true
```

Public keys are:

| Key | Values | Default |
| --- | --- | --- |
| `mode` | `verified`, `paper` | `verified` |
| `level` | `light`, `balanced`, `medium`, `heavy` | `balanced` |
| `seed` | unsigned 64-bit integer | OS randomness |
| `functions` | `annotated`, `all`, `regex:<pattern>` | `annotated` |
| `stats` | boolean | `false` |
| `diagnostics` | boolean | `false` |

`transform`, `probability`, and `depth` exist for deterministic tests and research runs, not as primary wrapper switches. Unknown keys and malformed values are hard errors.

## Profiles

The paper gives depth ranges for Light, Medium, and Heavy but no exact selection probabilities. A²MBA-LLVM keeps those ranges, adds `balanced` as a lower-cost default, and uses the following probabilities:

| Profile | Depth | Candidate | Architectural family | Context Trap within AGT |
| --- | ---: | ---: | ---: | ---: |
| `light` | 2-4 | 35% | 20% | 15% |
| `balanced` | 3-6 | 55% | 20% | 20% |
| `medium` | 8-12 | 70% | 30% | 25% |
| `heavy` | 16-24 | 100% | 35% | 30% |

The manager chooses an allowed transform family at each layer rather than nesting one fixed primitive repeatedly.

## Randomness and constants

Without `seed`, `RandomSource` reads from the operating system: `getrandom` on Linux and `BCryptGenRandom` on Windows. Supplying a seed switches to a deterministic stream for tests, bug reports, and comparisons.

Rule Explosion uses nonzero odd `APInt` constants so an inverse exists modulo `2^32` or `2^64`. A module-wide registry prevents reuse. Context Trap independently varies its shift from 1 through 8, a nonzero subset of the affected low bits, and one of six equivalent reconstruction trees. Seeded determinism means the same source IR, plugin build, configuration, target, and pipeline produce the same transform plan; it is not a promise that unrelated linkers or build metadata are deterministic.

## Architectural primitives

ADC and SBB identities depend on x86 carry state. Their state-sensitive sequence is emitted as one side-effecting inline-assembly unit with explicit condition-code and memory effects. The unit saves/restores flags around the carry-dependent operation and marks affected SysV functions `noredzone` because stack-based flag preservation must not overlap the red zone.

The primitive stays atomic so neither the optimizer nor the scheduler can move unrelated work between flag setup and consumption.

## Observability

Generated and protected metadata serve diagnostics and tests only; program behavior must not depend on them. `--stats` prints aggregate visited, selected, transformed, family, and skip counters. Diagnostics explain skips such as poison flags or an unsupported forced transform.
