# Design

## Pipeline placement

`llvmGetPassPluginInfo()` registers `a2mba` with LLVM 21's New Pass Manager. `opt` can run it explicitly; Clang runs it at the optimizer-last extension point:

```text
C or C++
  -> normal Clang optimization
  -> A²MBA module pass
  -> LLVM backend
  -> object or executable
```

InstCombine, GVN, SCCP, and the rest of the normal optimizer run first. Running them again after A²MBA could undo the inserted identities. For IR inspection or experiments, use `opt -passes=a2mba` explicitly.

ThinLTO and Full LTO are not supported in v0.1. The pass skips those phases because its final position has not been validated there.

The official portable Windows LLVM package disables loadable plugins. On that build, `a2mba-opt` links the pass into the driver and uses bitcode between the pass and Clang's backend:

```text
C or C++
  -> normal Clang optimization and bitcode
  -> a2mba-opt
  -> serialized protected bitcode
  -> Clang backend with LLVM passes disabled
  -> object or executable
```

The pass and driver share one LLVM image, including its analysis identities and container hash seed. Clang receives serialized bitcode, not the pass's in-memory LLVM objects, and backend optimization is disabled after the rewrite.

## Module driver, function-local changes

The authors used a Function Pass. `A2MBAPass` instead drives function-local rewrites from module scope so these values have one lifetime:

- parsed configuration;
- deterministic or OS-backed randomness;
- the module-wide set of constants already used by Rule Explosion;
- accumulated statistics;
- processed/generated metadata.

The planner does not query host analyses to decide whether a transform is safe. Context Trap, for example, constructs its valid trap range with masks rather than relying on ScalarEvolution. The same planner therefore works in the plugin and standalone driver.

The pass collects eligible source instructions before changing any of them. It tags new instructions with `!a2mba.generated` and marks processed functions and modules. Neither generated instructions nor a module seen by a second `a2mba` run get expanded again.

## Function selection

Only annotated functions are selected by default:

| Value | Behavior |
| --- | --- |
| `functions=annotated` | Select functions carrying the `a2mba` source annotation. This is the default. |
| `functions=all` | Select every otherwise eligible function. |
| `functions=regex:<pattern>` | Select matching LLVM function names. |

`sdk/a2mba.h` defines `A2MBA_PROTECT`, `A2MBA_PROTECT_NOINLINE`, and `A2MBA_IGNORE`. Use the `NOINLINE` form if the function must survive optimization as a distinct boundary. Annotations affect selection at compile time only.

## Eligibility

The pass accepts scalar `i32` and `i64` `add`, `sub`, `mul`, `and`, `or`, and `xor` instructions. It skips:

- generated or dead instructions;
- functions that already contain user inline assembly;
- other integer widths and vectors;
- floating-point, pointer, memory, call, and control-flow operations;
- instructions with `nsw` or `nuw` poison-generating flags;
- a transform that cannot preserve the selected opcode.

In particular, stripping `nsw` or `nuw` to apply a modulo identity would change LLVM semantics. Such instructions stay untouched.

## Configuration

The pass reads `A2MBA_OPTIONS` once per module. Use semicolon-separated `key=value` entries:

```text
mode=verified;level=balanced;seed=1337;functions=annotated;stats=true
```

Public keys are:

| Key | Values | Default |
| --- | --- | --- |
| `mode` | `verified`, `paper` | `verified` |
| `level` | `light`, `balanced`, `medium`, `heavy` | `balanced` |
| `hybrid` | `off`, `ir`, `native` | `off` |
| `hybrid-region` | `none`, `stateful` | `none` |
| `hybrid-layers` | `none`, `profile`, `context-adc`, `context-sbb`, `context-random` | `none` |
| `seed` | unsigned 64-bit integer | OS randomness |
| `functions` | `annotated`, `all`, `regex:<pattern>` | `annotated` |
| `stats` | boolean | `false` |
| `diagnostics` | boolean | `false` |

`transform`, `probability`, and `depth` are mainly for deterministic tests and research runs. Unknown keys and malformed values fail the pass.

## Hybrid planner

Hybrid mode replaces the primary transform; it does not run another primary transform on top. The e-graph search has limits on nodes, matches, match steps, extraction depth, and AST size. Extraction depths run from 3 to 16, with a profile-specific minimum AST size to rule out trivial spellings of the source operation. Stateful regions can fall back to a seed operation, as described below. `ir` emits the chosen DAG as LLVM instructions and may put Context Trap inside it. `native` checks instruction and scratch-register budgets before emitting one register-only x86-64 assembly block. If native lowering does not fit, it does not silently switch to IR.

After either emitter, a nonlinear envelope wraps the result. Its odd multiplier depends on both source operands; the emitted IR computes the modular inverse. The product cancels modulo `2^w`, including when the base expression came from native assembly.

`hybrid-layers=none` leaves the envelope in place but adds no outer layer. `profile` uses the active profile's Context Trap and architectural probabilities. `context-adc` and `context-sbb` require Context Trap plus the named gadget; `context-random` requires Context Trap and chooses ADC or SBB from the configured random stream. If a required layer cannot be emitted, the source instruction is skipped.

Equality search finishes before Context Trap or ADC/SBB is added. The algebraic rule engine never sees the stateful or context-sensitive layers. Outside region mode, a planning failure skips the instruction and increments its own counter.

### Stateful regions

`hybrid-region=stateful` groups straight-line chains within a basic block. A region needs at least two eligible operations. Each operation after the first uses the previous result and may also use an argument, constant, or value computed before the region starts. That includes loads and PHIs. An instruction defined partway through the chain cannot become a region input because the encoder stabilizes inputs at the start. Intermediate results must each have one use. The final result may have several users, but no PHI user. Operations outside a region still use ordinary per-operation planning.

Each operation first tries bounded hybrid planning. On failure, the region planner may use the original operation as a seed while retaining the nonlinear encoding and requested layers. Native mode still enforces its lowering budgets. For protected result `v_i`, external context `c_i`, and salt `t_i`, the runtime state changes as follows:

```text
s_(i+1) = (s_i xor v_i) * ((s_i xor c_i) or 1) + t_i  mod 2^w
```

An odd key derived from the new state and both inputs encodes the value. Only the final result is decoded outside the region. There is no independent output to extract after every operation, though a whole-region symbolic analysis can still recover the bit-vector identity.

## Profiles

The authors give depth ranges for Light, Medium, and Heavy, but no selection probabilities. This pass keeps those ranges and adds a lower-cost `balanced` default:

| Profile | Depth | Candidate | Architectural family | Context Trap within AGT |
| --- | ---: | ---: | ---: | ---: |
| `light` | 2-4 | 35% | 20% | 15% |
| `balanced` | 3-6 | 55% | 20% | 20% |
| `medium` | 8-12 | 70% | 30% | 25% |
| `heavy` | 16-24 | 100% | 35% | 30% |

At each layer, the manager picks an eligible transform family; depth does not mean repeating one primitive.

## Randomness and constants

Without `seed`, `RandomSource` uses `getrandom` on Linux or `BCryptGenRandom` on Windows. Set a seed to reproduce a transform plan in a test or bug report.

Rule Explosion chooses nonzero odd `APInt` constants so an inverse exists modulo `2^32` or `2^64`; a module-wide registry prevents reuse. Context Trap varies the shift (1–8), a nonzero subset of affected low bits, and one of six reconstruction trees. Nonlinear envelopes vary salts, factors, inverse circuits, and decode order. A seed reproduces the plan only with the same IR, build, settings, target, and pipeline. It does not make linker output or build metadata reproducible.

## Architectural primitives

ADC and SBB depend on x86 carry state. The pass emits carry setup, use, and compensation in one side-effecting assembly block with explicit condition-code and memory effects. Carry setup can use `stc`, `clc` plus `cmc`, a bit test of a known odd constant, or compare plus complement. Compensation order varies too.

Some variants preserve flags with `pushfq` and `popfq`; others declare the flags clobbered and do not use the stack. SysV functions that may use stack-based flag preservation are marked `noredzone`.

Neither the optimizer nor the scheduler can move unrelated work between carry setup and the instruction that consumes it.

## Observability

Generated/protected metadata is for diagnostics and tests, not program behavior. `--stats` prints visited, selected, transformed, family, and skip totals. Diagnostics give the reason for a skip, such as poison flags or an unsupported forced transform.
