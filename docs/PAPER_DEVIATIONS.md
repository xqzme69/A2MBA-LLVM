# Intentional deviations from the paper

The original prototype's source is not available in this repository. Where the paper omits an implementation detail, A²MBA-LLVM uses a documented LLVM 21 choice. Those choices belong to this project rather than the authors' prototype.

## Toolchain and pass model

| Topic | Paper | This project | Reason |
| --- | --- | --- | --- |
| LLVM | 15 | 21.x only | Current pass-plugin infrastructure and a narrow, testable compatibility target. |
| Pass scope | Function Pass | Module driver, function-local rewrites | One lifetime for used constants, randomness, configuration, metadata, and statistics. |
| Registration | Prototype pipeline | New-PM pipeline parser plus optimizer-last callback | Works with stock LLVM 21 `opt` and Clang without a compiler fork. |
| Language/build | C++ prototype | C++20; `add_llvm_pass_plugin` when LLVM enables plugins, otherwise a Windows compatibility module | Supports both plugin-enabled LLVM 21 builds and the official portable Windows 21.1.8 package. |

## Architectural lowering

The paper describes inline assembly for flag handling and prefers an x86 carry intrinsic for the arithmetic operation. Here, the complete state-sensitive primitive stays in one side-effecting inline-assembly unit. That unit owns flag save, setup, use, restore, clobbers, and result.

LLVM optimization and machine scheduling therefore cannot separate `STC` from ADC/SBB or insert work inside the sandbox. Plugin-enabled builds mark affected Linux-target functions `noredzone`.

## Windows portable LLVM compatibility

The official portable Windows LLVM 21.1.8 package reports `LLVM_ENABLE_PLUGINS=OFF`. A²MBA therefore builds a compatibility DLL linked to the LLVM components it needs. This is build engineering, not a technique from the paper.

The host and DLL then contain separate static LLVM copies. The DLL remains pinned until the compiler or `opt` exits because pass-created LLVM objects may outlive the loader handle. The pass does not query analysis managers across this boundary.

The Windows wrapper does not send a module changed by that DLL directly into Clang's backend. It asks Clang for optimized bitcode, runs the compatibility DLL under the matching `opt`, serializes the result, and starts Clang again for lowering with LLVM optimization passes disabled. The process boundary removes plugin-owned in-memory LLVM objects while retaining optimizer-last placement. Direct `opt -passes=a2mba` use is unchanged.

For the same reason, the compatibility path does not pass an LLVM `AttributeList` back to the host to add `noredzone`. When it processes Linux-target IR, each affected AAMBA primitive instead uses one atomic inline-assembly unit with this shape:

```text
leaq -128(%rsp), %rsp
pushfq
... STC and the state-sensitive operation ...
popfq
leaq 128(%rsp), %rsp
```

`LEA` is flag-neutral, while `pushfq` and `popfq` retain the full save/restore contract. The inline red-zone clearance replaces only the unsafe cross-DLL attribute update. Plugin-enabled builds still use `noredzone` for Linux targets, and the compatibility DLL never becomes a runtime dependency of the protected executable.

## RCR/RCL

The paper's Equation (4) describes `RCR(RCL(x,1),1)` as an identity only for a particular initial carry state. The operations are inverses on the combined carry-plus-register state for either initial carry. As a result:

- `verified` mode does not use the pair;
- `paper` mode retains it as an explicitly paper-oriented transform;
- regression tests keep the two modes distinct.

## Context Trap pair

The paper explains separate positive trap and negative trigger contexts. Applying those preconditions directly to arbitrary source operations would be unsafe, so this project constructs both contexts around the completed operation instead.

The trap arm masks the value into a range where its signed shift is exact. The trigger arm forces the bit that becomes the sign after the left shift, then removes the resulting extension bit. A correction term from the trap and a high/low reconstruction make the complete expression equal to the original value for every input. The implementation randomizes the shift, selected low bits, and one of six equivalent reconstruction trees. This is a project extension of the paper's learning attack, not a claim about the unpublished prototype's exact IR.

## Transform coverage

Equation (7) gives the addition form of Rule Explosion. The subtraction, multiplication, and operand-wrapping forms derive from the same modular scaling principle. They are project extensions, not unnamed equations from the paper.

Boolean operations are not distributed through modular multiplication. The manager recreates the original `and`, `or`, or `xor` and applies an allowed architectural identity to that result; later nesting layers may use `ModularScale`.

## Profiles

The paper publishes Light 2-4, Medium 8-12, and Heavy 16-24 depth ranges, but no exact candidate or transform-family probabilities. This project owns those probabilities and adds `balanced` at depth 3-6 as its default.

## LLVM semantics

The paper formulas use fixed-width machine arithmetic. LLVM's `nsw` and `nuw` flags add poison semantics beyond ordinary modulo arithmetic. v0.1 skips flagged candidates rather than stripping flags or assuming overflow cannot occur.

Generated instructions are marked and never revisited. Processed module metadata makes a second pass invocation idempotent. Both safeguards belong to this implementation.

## Randomness

The paper describes cryptographically secure, unique 64-bit constants. This project uses the platform OS source by default and provides a seeded deterministic stream for repeatable tests and bug reports. The same stream selects Context Trap shifts, masks, and reconstruction forms. Seeded mode is intentionally reproducible and therefore not cryptographically unpredictable.

Constants and inverses are width-specific `APInt` values. The registry is module-wide because the driver has module scope.

## Platform and pipeline limits

v0.1 supports Linux and Windows x86-64, scalar `i32`/`i64`, and normal non-LTO compilation. ARM, RISC-V, vectors, arbitrary widths, ThinLTO, and Full LTO are outside the contract. Unsupported cases are skipped or rejected, never counted as silently protected.

## Evaluation

The paper's resilience, deception, correctness, diversity, runtime, and size numbers describe its own prototype and experiment. The project publishes a separate, forced depth-one Context Trap snapshot in [BENCHMARKING.md](BENCHMARKING.md); it does not reproduce the paper's evaluation or transfer the paper's results to A²MBA-LLVM.
