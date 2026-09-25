# Security model

## Goal

A²MBA targets automated analysis of selected integer expressions. It uses:

- x86 architectural state that a stateless algebraic model may omit;
- parameterized modular identities that reduce reuse of constant-specific rewrite rules;
- parameterized trap and trigger contexts that preserve the source value but expose an unsound generalized shift rule;
- input-dependent nonlinear multiplication that falls outside the linear MBA basis;
- optional state carried across a conservative straight-line region instead of independent per-operation encodings;
- seeded or OS-random transform selection to vary protected outputs;
- bounded equality-saturation and optional native expansion to vary the base expression before context-sensitive layers are added.

The aim is to increase the work needed to analyze them. It does not make the code secret or irreversible.

## Attacker

Assume the attacker controls the machine running the program. They can:

- read and modify binaries and memory;
- execute, trace, emulate, and debug the program repeatedly;
- compare protected variants;
- use disassemblers, symbolic execution, SMT, algebraic MBA simplifiers, and learned rewrite systems;
- recognize that A²MBA was used and inspect this source code.

The attacker also knows this repository, its algorithms, metadata names, and wrapper configuration.

## Defender assumptions

- The supported LLVM 21 plugin is loaded into a compatible Clang/LLVM process.
- The target is x86-64 Linux or Windows for architectural primitives.
- Only eligible scalar `i32`/`i64` operations are transformed.
- The verified mode and its conservative LLVM poison checks are used for production builds.
- Normal compiler, linker, dependency, and release integrity controls remain in place.
- Real secrets are managed cryptographically and are not assumed safe merely because their surrounding arithmetic is obfuscated.

## Expected resistance

A simplifier that treats ADC as ordinary addition can get the wrong result because it loses carry. Context Trap targets a separate mistake: learning a rule from the masked trap arm and applying it to the trigger without its context, or treating arithmetic right shift as logical. Shifts, masks, and reconstruction trees vary between sites. A sound simplifier needs the local context and fixed-width signed-shift semantics.

The nonlinear envelope builds an odd runtime key from a multiplication with input-dependent operands on both sides. The emitted expression is outside the linear-MBA grammar, but it still computes the source function and may be canceled algebraically. The post-`-O3` regression traces input dependencies; it checks the surviving IR shape, not how hard that IR is to simplify.

Stateful regions remove the convenient boundary between separately protected operations. One decoded result changes the state for the next encoding, and intermediate values stay inside the region. An analyzer that keeps the whole region can still model its transitions. The state is not a cryptographic secret.

An architecture-aware emulator or symbolic executor can model the flags. A nonlinear simplifier may prove the inverse relation; a rule learner can use guards instead of generalizing away the context. The gadget families can also be recognized by a human.

## Windows driver boundary

The official portable Windows LLVM 21.1.8 package disables loadable plugins. `a2mba-opt.exe` links LLVM's opt entry point and the pass into one driver, avoiding two static LLVM copies sharing objects in one process. This is a build correctness measure, not a protection layer. The compiled program does not depend on the driver.

For optimized Windows builds, Clang first writes bitcode, `a2mba-opt.exe` protects it, and Clang lowers the serialized result with further LLVM optimization disabled. `--doctor` tests that path through object generation.

For Linux-target IR, stack-based flag preservation marks the function `noredzone`. Flag-clobbering variants do not touch the stack.

## Out of scope

A²MBA does not claim to provide:

- protection against a determined human reverse engineer;
- control-flow obfuscation, anti-debugging, packing, virtualization, or tamper resistance;
- side-channel resistance or constant-time execution;
- cryptographic key protection by itself;
- memory safety, control-flow integrity, or exploit mitigation;
- protection of floating-point, vectors, pointers, memory accesses, calls, or exception handling;
- supported ThinLTO or Full LTO integration in v0.1;
- an assurance that every annotated function contains an eligible operation;
- the resilience or overhead numbers reported by the paper's separate prototype.

## Mode choice

Use `verified` for normal builds. It leaves out the RCR/RCL carry-dependence claim and uses a Context Trap identity valid for all fixed-width inputs. `paper` adds the comparison transform; it is not a stronger protection setting.

Hybrid mode is experimental and off by default. Its extraction score favors certain expression shapes and costs; it is not a resilience score. The nonlinear envelope removes the old all-linear syntax, not the possibility of a sound nonlinear simplification. Stateful regions make per-operation interpolation less representative, while whole-region symbolic execution remains possible. Native lowering is not encryption.

Measure on the target program before choosing a profile. Heavy transforms can add substantial runtime and binary size and may themselves stand out.

## Failure behavior

The pass skips unsupported or unproven candidates. Bad configuration fails instead of silently weakening the requested transform. The wrapper rejects an unsupported LLVM major or mismatched Windows driver, and `--doctor` fails if the pipeline cannot produce an object.

Selection and skip counts tell you what was transformed, not how resistant it is.

## Deployment guidance

- Annotate narrow, high-value functions instead of selecting an entire application by default.
- Keep reference and protected functional tests, including edge values and real workloads.
- Run `--doctor`, the lit/CTest suite, and code-generation checks with the exact release toolchain.
- Keep the post-`-O3` nonlinear dependency and architectural-diversity gates enabled; instruction counts or binary size alone do not establish hardening.
- Keep lit's `clang`, `opt`, `llc`, and normally `FileCheck` on that same LLVM 21 release. Use `A2MBA_FILECHECK_EXECUTABLE` only when FileCheck must be supplied separately.
- Measure deployment overhead on the target hardware. The paper's factors describe a different implementation.
- Strip or retain LLVM metadata according to normal release policy, but do not treat stripping as a security boundary.
- Combine A²MBA with ordinary secure design, signing, update integrity, and cryptographic controls where those properties are required.
