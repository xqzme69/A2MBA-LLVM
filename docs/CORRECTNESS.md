# Correctness

Seeing a larger IR graph or an ADC opcode says nothing about whether the rewritten program still computes the same result. The checks below cover different parts of that question.

## Semantic domain

The identities use scalar `i32` and `i64` arithmetic modulo `2^w`. `APInt` handles constants, inverses, and truncation, including the `2^64` modulus that does not fit in a host `uint64_t`.

The core modular identities are:

```text
C is odd and nonzero
C * C_inv = 1 (mod 2^w)
((x*C) + (y*C))*C_inv = x + y (mod 2^w)
(x*C)*C_inv = x (mod 2^w)
```

Native self-tests cover inverse helpers and these identities with edge cases and generated values. IR tests check constant widths.

## LLVM undef and poison

An LLVM instruction such as:

```llvm
%sum = add nsw i32 %lhs, %rhs
```

is not plain modulo addition: signed overflow produces poison. The pass skips arithmetic with `nsw` or `nuw` instead of removing the flags.

Unsupported types and side-effecting operations are also left alone.

An SSA name does not guarantee one stable value at every use. Each use of `undef`, or of an instruction derived from it, can see different bits. `mul i64 %x, 2` always has a clear low bit, even with `undef` `%x`; `add i64 %x, %x` need not. Duplicating an operand is therefore not justified by a fixed-width algebraic identity alone.

Before expansion, the pass freezes any operand LLVM cannot prove free of undef and poison. Repeated uses share the same freeze. A stateful region stabilizes each external input once, before it derives initial state. Defined inputs such as `noundef` arguments and existing `freeze` results need no extra freeze.

For defined inputs this preserves behavior. For undef or poison it chooses a stable concrete value, a refinement LLVM permits; exact poison propagation is not preserved. The undef regression checks masks and even-number invariants after fresh optimization and executes classic, per-operation hybrid, and stateful output at `-O0` and `-O3`. IR checks cover shared and derived inputs and reject redundant freezes.

## Context Trap proof

Choose `1 <= k <= 8` and nonzero `Q` within the low `k` bits. Set `F = 2^(w-k-1)`, `P = 2^(w-k)`, `S = F - 1`, and `M = P | Q`. The trap arm is:

```text
trap = ashr((v & S) << k, k) & M
```

`S` clears the sign source and bits the left shift could discard. The arithmetic shift restores `v & S`; masking with `M` leaves `v & Q`.

The trigger arm is:

```text
trigger = (ashr((v | F) << k, k) & M) ^ P
```

Forcing `F` sets the sign after the left shift. Arithmetic right shift sets `P`; the mask retains `P` and the `Q` bits; XOR removes `P`. So `trigger = v & Q` for every input. One reconstruction is:

```text
(v & ~Q) | ((trigger + trap) - (v & Q)) = v
```

There is no input-range precondition. Five other reconstructions combine the same three equal low projections. IR tests check all six at both widths, and differential tests execute each on edge values.

The reconstructed value then enters the nonlinear envelope. That changes the data-flow graph, not the Context Trap identity.

## Nonlinear envelope proof

The emitter combines the source operands into two factors using Boolean and arithmetic operations. Their product `p` yields odd `K = 2p + 1`; the final odd-bit step may be emitted as OR, addition, or XOR. Any odd `K` has an inverse modulo `2^w`.

For the Newton variant, an initial one- or two-bit approximation gains twice as many correct inverse bits at each step. The emitter varies among three equivalent forms:

```text
i' = i * (2 - K*i)
i' = i + i * (1 - K*i)
i' = 2*i - K*i*i
```

After enough iterations, `K*i = 1 (mod 2^w)`. The envelope can return `v*K*i`, `v*i*K`, or `v*(K*i)`; each equals `v`. The unit test checks all odd keys at 8 and 16 bits and deterministic large samples at 32 and 64 bits. The alternative finite-product inverse is described in [Transforms](TRANSFORMS.md).

## Stateful region proof

At each region step, state may take any `w`-bit value; the transition need not be invertible. The encoded value is `e = (v+b)*K`, with state-dependent bias `b` and odd `K = 2p+1`. The inverse satisfies `K*K_inv = 1 mod 2^w`, so:

```text
e*K_inv-b = ((v+b)*K)*K_inv-b = v  mod 2^w
```

The next operation receives decoded `v`. Repeating that argument along the chain gives each operation its original input, and the exit decode equals the original result. The model test runs 50,000 deterministic encode/decode cases at each width.

The runtime test uses i32/i64 chains with all six supported operations, the `(x ^ x) - y` seed fallback, and a conditional function whose regions sit in separate blocks. It checks six edge vectors and 1,024 generated vectors for IR and native output, with and without outer layers, at `-O0` and `-O3`. Statistics must show five transformed regions; LLVM verifies each module before execution.

The seed sweep has 24 chains of two to seven operations, including reversed operands and boundary constants. It uses light/0, balanced/17, medium/9223372036854775809, and heavy/18446744073709551615. Each profile/seed pair runs IR and native output, with and without `context-random`, at `-O0` and `-O3`. A separately compiled host checks 272 vectors against Python's fixed-width results: 208,896 comparisons across 32 protected executables. Every transformed module must report 24 regions and 108 source operations. Deliberately wrong output must fail the same oracle.

Boundary tests reject cross-block regions, load/PHI inputs, poison-generating flags, and intermediate fan-out. They also check multiple users of the final result, independent chains, and an interleaved volatile store.

The Windows DLL regression builds two C++ translation units with `heavy`, stateful regions, and `context-random`, once with IR emission and once with native emission. Statistics must confirm three regions and the selected emitter. A separate host loads the DLL and checks 16,400 i32/i64 results per variant, including eight-argument calls and four-thread execution. It also checks callbacks, a C++ exception crossing a transformed DLL function, destructors, and a normal call afterward. Both sides use the DLL CRT. This covers those integer-call and synchronous-exception paths, not every Windows ABI or unwind case.

## Architectural state

ADC/SBB correctness includes more than the returned integer:

- carry is established before the instruction that consumes it;
- the complete carry-dependent sequence stays in one atomic side-effecting assembly block;
- a variant either restores flags or declares them clobbered;
- clobbers prevent LLVM from treating the block as pure or freely reorderable;
- stack-based flag preservation is paired with red-zone protection on SysV x86-64;
- i32 and i64 use matching instruction widths.

Code-generation tests check the carry instructions and reject a corpus with one repeated gadget skeleton. Runtime and differential checks cover the rest: an ADC or SBB opcode does not prove the compensation or flag contract is correct.

RCR/RCL stays in `paper` mode because its claimed initial-carry dependency is not used as a verified premise.

## Rewrite discipline

The pass collects source candidates before rewriting. New instructions get `!a2mba.generated`, protected functions get `!a2mba.protected`, and module metadata records completion. A second run does not expand the generated IR.

It redirects uses to the completed replacement before erasing the original instruction. IR tests run LLVM's verifier for malformed types, dominance errors, and broken use lists.

## Hybrid rule admission

`lib/HybridCore/mba.json` is the source for the hybrid rewrite table. Before generating C++, Z3 must find each rule's negation unsatisfiable at 8, 16, 32, and 64 bits. SAT, UNKNOWN, timeout, solver failure, or an unbound metavariable stops the build step. Incremental builds reuse the generated files until the rules or verification scripts change.

That gate checks the listed bit-vector rules. It does not prove the e-graph, extractor, LLVM adapter, or x86 lowerer. Core tests check graph invariants, cyclic classes, deterministic extraction, native budgets, and differential evaluation. Lit covers IR output, native code generation, runtime behavior, configuration errors, and counters through the integrated pass.

The hardening regression reoptimizes hybrid and Context Trap output with a fresh `default<O3>`. It traces SSA values to function arguments and requires several `mul` operations with input dependence on both sides. A textual `mul %ssa, %ssa` check would miss operands derived only from constants.

## Determinism

With an explicit seed, transform choices and constants are deterministic for the same:

- input LLVM IR;
- A²MBA build;
- full `A2MBA_OPTIONS` value;
- LLVM target and pass pipeline.

Lit compares same-seed IR byte for byte and checks that another seed changes it. `scripts/diversity.py` does the analogous SHA-256 check on object files built from a supplied source.

It does not imply reproducible executables across linkers, paths, timestamps, LLVM patch versions, or hosts.

## Test layers

The tests are split by what they check:

| Layer | What it checks |
| --- | --- |
| native self-test | modular inverse and deterministic random stream properties |
| nonlinear envelope model | inverse refinement and decode forms over 8/16/32/64-bit keys |
| stateful region model | affine encode/decode identities with state-derived keys over i32/i64 |
| baseline harness | unsupported IR/shift rejection, architectural projection checks, bounded export, a solver proof of i64 shift normalization, and executable checksum verification with a deliberately incorrect workload |
| validation helpers | region-mode argument forwarding, benchmark JSON configuration, deterministic COFF timestamp flags on Windows, and compiled stateful sample runs |
| hybrid core self-test | e-graph invariants, bounded extraction, native lowering, and differential evaluation |
| `test/IR` | transform shapes, widths, operand stabilization, all Context Trap reconstructions, poison/unsupported skips, and nonlinear data flow after a fresh `-O3` |
| `test/Plugin` | plugin loading, explicit pipeline, annotation selection, and Clang integration |
| `test/CodeGen` | x86 ADC/SBB lowering, carry-setup diversity, compensation diversity, and both flag modes |
| `test/Runtime` | reference/protected results, undef invariants, stateful chains and seed fallback, plus a multi-profile seed sweep against an independent fixed-width oracle |
| `test/Negative` | malformed configuration is rejected |
| `test/Regression` | generated IR is not recursively expanded |
| `test/Determinism` | same-seed stability and different-seed variation |
| `test/Wrapper` | LLVM/plugin discovery, annotated C execution, Windows staged compilation, working directories, option-value collisions, response files, failure propagation across compilation stages, plus C++ DLL integer calls, concurrent execution, and exception cleanup |

Run all configured tests with:

```bash
cmake --build build --config Release --target check-a2mba
ctest --test-dir build -C Release --output-on-failure
```

The table is a coverage map, not a claim that every row passed on a given machine.

## Remaining assurance limits

No machine-checked proof covers the whole LLVM pass. Differential tests sample inputs. The nonlinear regression rules out the old all-linear IR shape, and the stateful regression rules out isolated per-operation output; neither proves resistance to a general nonlinear simplifier, whole-region SMT, or symbolic execution. Assembly and optimizer integration still need code-generation and runtime checks for each supported toolchain. `paper` mode is intentionally outside the verified default.
