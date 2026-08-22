# Security model

## Goal

A²MBA raises the cost of automated analysis for selected integer data-flow expressions through:

- x86 architectural state that a stateless algebraic model may omit;
- parameterized modular identities that reduce reuse of constant-specific rewrite rules;
- parameterized trap and trigger contexts that preserve the source value but punish an unsound generalized shift rule;
- seeded or OS-random transform selection to vary protected outputs.

The intended property is higher analysis cost, not secrecy or irreversibility.

## Attacker

Assume a Man-at-the-End attacker controls the machine running the program and can:

- read and modify binaries and memory;
- execute, trace, emulate, and debug the program repeatedly;
- compare protected variants;
- use disassemblers, symbolic execution, SMT, algebraic MBA simplifiers, and learned rewrite systems;
- recognize that A²MBA was used and inspect this source code.

Assume the attacker knows the plugin, algorithms, metadata names, and wrapper configuration.

## Defender assumptions

- The supported LLVM 21 plugin is loaded into a compatible Clang/LLVM process.
- The target is x86-64 Linux or Windows for architectural primitives.
- Only eligible scalar `i32`/`i64` operations are transformed.
- The verified mode and its conservative LLVM poison checks are used for production builds.
- Normal compiler, linker, dependency, and release integrity controls remain in place.
- Real secrets are managed cryptographically and are not assumed safe merely because their surrounding arithmetic is obfuscated.

## Expected resistance

A stateless simplifier that treats ADC as ordinary addition can derive the wrong result. Context Trap targets a different modeling error: generalizing a rule learned from its masked trap arm while treating arithmetic right shift as logical right shift. Random shifts, selected-bit masks, and reconstruction trees prevent one syntactic rule from covering every site. Handling the transform correctly still requires signed-shift semantics, fixed-width arithmetic, and local context in the analysis model.

None of this is a permanent barrier. An architecture-aware emulator or symbolic executor can model flags, modular inverses are recognizable, and a human can identify the assembly templates. A deobfuscator can also learn guarded rules instead of context-free ones.

## Windows compatibility boundary

The official portable Windows LLVM 21.1.8 package disables LLVM's normal plugin build path. A²MBA's compatibility DLL links the required LLVM components statically, remains pinned until Clang or `opt` exits, and does not query host analysis managers across that boundary. Pinning prevents compiler-process lifetime errors; it adds no security, and the produced application does not retain the DLL.

For optimized machine-code builds, the Windows wrapper runs A²MBA under `opt` between two Clang stages. The protected module crosses into the backend as serialized bitcode, not as LLVM objects allocated by the compatibility DLL. The final Clang stage disables LLVM optimization passes so the protected form is not fed through a second optimizer pipeline. `--doctor` exercises this complete staged path, including object generation.

LLVM object identities cannot safely cross between the host and the DLL's static LLVM copy, so this path does not pass an `AttributeList` back to add `noredzone`. For Linux-target IR, each affected AAMBA primitive uses one atomic assembly unit: flag-neutral `leaq -128(%rsp), %rsp`, the existing `pushfq`/`popfq` sandbox, then `leaq 128(%rsp), %rsp`. This clears the SysV red zone before stack use without changing flag preservation. Plugin-enabled builds keep the normal `noredzone` attribute.

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

Use `verified` outside paper-oriented experiments. It excludes the disputed RCR/RCL carry-dependence argument and keeps Context Trap as a universal bit-vector identity. `paper` adds a documented comparison transform; it is not a stronger setting.

Start with the lightest profile that meets a measured need. Heavy transformation can raise runtime and binary size enough to become a signature of its own.

## Failure behavior

Unsupported or unproven candidates are skipped, never approximately transformed. Malformed configuration is a hard plugin error. The wrapper rejects an unsupported LLVM major or a mismatched Windows `opt` before compilation, and `--doctor` fails if the selected pipeline cannot produce an object.

Statistics and diagnostics describe the build. Selection and skip counts are not a security score.

## Deployment guidance

- Annotate narrow, high-value functions instead of selecting an entire application by default.
- Keep reference and protected functional tests, including edge values and real workloads.
- Run `--doctor`, the lit/CTest suite, and code-generation checks with the exact release toolchain.
- Keep lit's `clang`, `opt`, `llc`, and normally `FileCheck` on that same LLVM 21 release. Use `A2MBA_FILECHECK_EXECUTABLE` only when FileCheck must be supplied separately.
- Measure deployment overhead on the target hardware. The paper's factors describe a different implementation.
- Strip or retain LLVM metadata according to normal release policy, but do not treat stripping as a security boundary.
- Combine A²MBA with ordinary secure design, signing, update integrity, and cryptographic controls where those properties are required.
