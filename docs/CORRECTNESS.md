# Correctness

An obfuscation pass must preserve the program before its complexity matters. Larger IR or the presence of an expected opcode does not prove equivalence.

## Semantic domain

The supported identities operate on scalar `i32` and `i64` values modulo `2^w`. Constants, inverses, and truncation use `APInt`, including the `2^64` case that an ordinary host `uint64_t` cannot represent as a modulus.

The basic modular obligations are:

```text
C is odd and nonzero
C * C_inv = 1 (mod 2^w)
((x*C) + (y*C))*C_inv = x + y (mod 2^w)
(x*C)*C_inv = x (mod 2^w)
```

The native self-test checks inverse and identity helpers over fixed edge cases and generated values. IR tests check that constants retain the candidate width.

## LLVM poison

An LLVM instruction such as:

```llvm
%sum = add nsw i32 %lhs, %rhs
```

is not plain modulo addition: signed overflow produces poison. Replacing it with an unflagged modular identity would change when the program is defined. The eligibility layer skips integer operations carrying `nsw` or `nuw`; it never strips those flags to force a transform.

Unsupported types and side-effecting operations are left alone for the same reason. A conservative skip is expected behavior.

## Context Trap proof

The planner chooses `1 <= k <= 8` and a nonzero mask `Q` contained in the low `k` bits. Let `F = 2^(w-k-1)`, `P = 2^(w-k)`, `S = F - 1`, and `M = P | Q`. The trap arm is:

```text
trap = ashr((v & S) << k, k) & M
```

`S` clears the sign source and every bit that the left shift could discard. The arithmetic shift therefore restores `v & S`, and masking with `M` leaves exactly `v & Q`.

The trigger arm is:

```text
trigger = (ashr((v | F) << k, k) & M) ^ P
```

Forcing `F` makes the shifted sign bit one. Arithmetic right shift sets `P`, the mask retains `P` and the bits selected by `Q`, and XOR removes `P`. Thus `trigger = v & Q` for every fixed-width input. One emitted reconstruction is:

```text
(v & ~Q) | ((trigger + trap) - (v & Q)) = v
```

No input-range assumption is part of this identity. Five additional reconstructions use equivalent operations on the three equal low projections. IR tests cover all six forms and both widths, while runtime differential tests execute every form over representative edge values.

## Architectural state

ADC/SBB correctness includes more than the returned integer:

- carry is established before the instruction that consumes it;
- flags are saved and restored within one atomic side-effecting assembly block;
- clobbers prevent LLVM from treating the block as pure or freely reorderable;
- stack-based flag preservation is paired with `noredzone` on SysV x86-64 functions;
- i32 and i64 use matching instruction widths.

Code-generation tests inspect the emitted assembly for the expected carry instruction families. Runtime and differential tests still matter: an ADC or SBB opcode alone says nothing about the compensation formula or restored flags.

The RCR/RCL pair is isolated to `paper` mode because the paper's explanation of its initial-carry dependency is not used as a verified premise.

## Rewrite discipline

The pass snapshots source candidates before rewriting. New instructions receive `!a2mba.generated`, protected functions receive `!a2mba.protected`, and module metadata records completed processing. A second pass run therefore cannot expand generated IR again.

All uses of the original candidate are redirected to the completed replacement before the original instruction is erased. LLVM's verifier is run by the IR test pipeline to catch malformed types, dominance, and use lists.

## Determinism

With an explicit seed, transform choices and constants are deterministic for the same:

- input LLVM IR;
- A²MBA build;
- full `A2MBA_OPTIONS` value;
- LLVM target and pass pipeline.

The lit determinism fixture compares same-seed IR byte for byte and requires a different seed to change it. `scripts/diversity.py` applies the same narrow check to compiled-object SHA-256 hashes for a supplied source file.

This does not assert reproducible executable files across different linkers, absolute paths, timestamps, LLVM patch versions, or host environments.

## Test layers

The repository organizes gates by claim:

| Layer | What it checks |
| --- | --- |
| native self-test | modular inverse and deterministic random stream properties |
| `test/IR` | transform shapes, widths, all Context Trap reconstructions, and poison/unsupported skips |
| `test/Plugin` | plugin loading, explicit pipeline, annotation selection, and Clang integration |
| `test/CodeGen` | x86 ADC/SBB lowering on Linux and Windows triples |
| `test/Runtime` | reference/protected results on representative i32/i64 edge values |
| `test/Negative` | malformed configuration is rejected |
| `test/Regression` | generated IR is not recursively expanded |
| `test/Determinism` | same-seed stability and different-seed variation |
| `test/Wrapper` | LLVM/plugin discovery and load smoke check |

Run all configured tests with:

```bash
cmake --build build --config Release --target check-a2mba
ctest --test-dir build -C Release --output-on-failure
```

This table describes test coverage, not the outcome of a particular run. Outcomes depend on the LLVM build, target, configuration, and commands used.

## Remaining assurance limits

There is no machine-checked proof of the complete LLVM pass. Differential tests sample behavior; they cannot cover every input. Inline assembly and optimizer integration also need code-generation and runtime checks for each supported platform/toolchain pair. `paper` mode remains outside the verified default by design.
