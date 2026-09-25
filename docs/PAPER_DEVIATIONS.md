# Intentional deviations from the paper

The authors' prototype is not in this repository. The table and notes below distinguish what their work specifies from the choices made for this LLVM 21 pass.

## LLVM 21 and the build path

| Topic | Paper | This project | Reason |
| --- | --- | --- | --- |
| LLVM | 15 | 21.x only | Current pass-plugin infrastructure and a narrow, testable compatibility target. |
| Pass scope | Function Pass | Module driver, function-local rewrites | One lifetime for used constants, randomness, configuration, metadata, and statistics. |
| Registration | Prototype pipeline | New-PM pipeline parser plus optimizer-last callback | Works with stock LLVM 21 `opt` and Clang without a compiler fork. |
| Language/build | C++ prototype | C++20; loadable plugin when LLVM enables plugins, otherwise a standalone Windows `opt` driver | Supports both plugin-enabled LLVM 21 builds and the official portable Windows 21.1.8 package without mixing static LLVM copies. |

## Architectural lowering

The authors describe inline assembly for flag handling and favor an x86 carry intrinsic for arithmetic. Here, one side-effecting assembly block handles carry setup, use, compensation, clobbers, and result. There is no separate carry operation for LLVM to move between instructions.

Carry setup, compensation order, and flag handling vary; the pass does not emit one fixed `STC` template. LLVM and the machine scheduler cannot insert work inside the block. When a Linux-target gadget saves flags on the stack, its function is marked `noredzone`.

The official portable Windows LLVM 21.1.8 package has `LLVM_ENABLE_PLUGINS=OFF`. On that package the pass is linked into `a2mba-opt.exe` with the standard opt driver. This is a toolchain workaround, not an obfuscation technique.

A compatibility DLL would put two static LLVM copies in one process. In assertion builds, `DenseMap` hashing uses an address in the LLVM image as a salt. The copies can then hash keys differently while sharing one `LLVMContext`; a rehash on a larger module can expose what a small smoke test misses.

The standalone driver has one LLVM copy and accepts `-passes=a2mba`. The Windows wrapper takes optimized bitcode from Clang, passes it through `a2mba-opt`, then lowers the serialized result in Clang with further LLVM optimization disabled. The pass still runs after normal optimization without sharing live LLVM objects across the two processes.

Both paths mark Linux-target functions `noredzone` when a gadget saves flags on the stack; flag-clobbering variants do not touch it. Neither the plugin nor the driver is needed to run the protected executable.

## Transform choices

Equation (4) treats `RCR(RCL(x,1),1)` as an identity under a particular initial carry. At the same width, the rotations undo each other on the combined carry/register state for either initial carry. Consequently:

- `verified` mode does not use the pair;
- `paper` mode retains it as an explicitly paper-oriented transform;
- regression tests keep the two modes distinct.

The authors describe positive trap and negative trigger contexts. An arbitrary source operation does not necessarily satisfy either precondition. This pass constructs both contexts around the completed operation instead.

The trap arm masks away bits that would be lost or sign-extended. The trigger arm forces the future sign bit, then removes the resulting extension bit. A correction term reconstructs the original value for every input. Shifts, selected low bits, and one of six reconstruction trees vary between sites.

The result then goes through a nonlinear modular envelope added by this project. It builds an odd key from input-dependent multiplication and computes an inverse in IR. This was not part of the authors' prototype as described in their work.

The authors evaluate transforms one instruction at a time. Our optional region mode links straight-line hybrid operations with runtime state and exposes a decoded value only at the exit. The matcher, state transition, and affine encoding were added here; their behavior cannot be inferred from the authors' experiments.

Equation (7) gives Rule Explosion for addition. The subtraction, multiplication, and operand-wrapping forms use the same modular scaling idea but were added here.

Modular multiplication does not distribute over `and`, `or`, or `xor`. For those operations, the manager recreates the original result, wraps it with an eligible architectural identity, and may add `ModularScale` in a later layer.

Light 2–4, Medium 8–12, and Heavy 16–24 are the published depth ranges. The candidate and family probabilities are ours, as is the default `balanced` profile at depth 3–6.

## LLVM semantics and randomness

The formulas use fixed-width arithmetic. LLVM `nsw` and `nuw` add poison on overflow, so v0.1 skips flagged instructions rather than stripping those flags.

Generated instructions carry metadata and are not revisited. Module metadata prevents expansion on a second pass invocation. Those safeguards are implementation choices here.

The authors call for cryptographically secure, unique 64-bit constants. By default this pass draws randomness from the OS. A seed gives reproducible tests and bug reports but makes the stream predictable. It also selects Context Trap parameters, nonlinear-envelope forms and salts, and gadget variants.

Constants and inverses use width-specific `APInt` values. A module-wide registry tracks constants already used by Rule Explosion.

## Supported builds and evidence

v0.1 covers Linux and Windows x86-64, scalar `i32`/`i64`, and non-LTO builds. It does not cover ARM, RISC-V, vectors, other widths, ThinLTO, or Full LTO. Unsupported candidates are skipped or rejected, not counted as protected.

The authors' rates and overhead belong to their prototype and test setup. Our older snapshots in [Benchmarking](BENCHMARKING.md) predate the current nonlinear hardening. Neither set of numbers measures the current implementation.
