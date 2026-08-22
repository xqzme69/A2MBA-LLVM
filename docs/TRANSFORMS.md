# Transforms

Every identity below uses fixed-width arithmetic modulo `2^w`, with `w` equal to 32 or 64. LLVM `APInt` handles the modulus and inverses without relying on host `uint64_t` overflow.

## Rule Explosion

For an addition and a nonzero odd constant `C`, choose `C_inv` such that:

```text
C * C_inv = 1 (mod 2^w)
```

Then:

```text
((x * C) + (y * C)) * C_inv = x + y (mod 2^w)
```

The generated LLVM IR is structurally:

```llvm
%x.scaled = mul i64 %x, C
%y.scaled = mul i64 %y, C
%sum.scaled = add i64 %x.scaled, %y.scaled
%result = mul i64 %sum.scaled, C_inv
```

Each instance gets a module-unique `C`. This implements Equation (7) from the paper. The related subtraction and multiplication forms are project extensions derived from the same scaling principle, not paper formulas.

## Modular Scale

The utility identity:

```text
(x * C) * C_inv = x (mod 2^w)
```

wraps an intermediate result without changing it. The manager can add it as a nesting layer after a primary transform. `ModularScale` is a project helper, not a separately named primitive from the paper.

## Context Trap

For an original value `v`, the planner chooses `k` from 1 through 8 and a nonzero mask `Q` drawn from the low `k` bits. Let:

```text
F = 2^(w-k-1)
P = 2^(w-k)
S = F - 1
M = P | Q
```

The trap arm clears every bit that could be lost or sign-extended, while the trigger arm forces the future sign bit:

```text
low        = v & Q
high       = v & ~Q
trap       = ashr((v & S) << k, k) & M
trigger    = (ashr((v | F) << k, k) & M) ^ P
result     = high | ((trigger + trap) - low)
```

On the CPU, the trap projection is `low`. The forced sign bit makes the trigger projection `low | P`; the XOR removes `P`, also leaving `low`. Therefore the protected low part is `(low + low) - low = low`, and `result = v` for every `i32` or `i64` input. The pass wraps the completed source operation, so it needs no guessed range precondition.

The displayed reconstruction is one of six emitted forms. The others combine the same equal `trap`, `trigger`, and `low` values with subtraction, XOR, OR, or disjoint addition. `k`, `Q`, and the reconstruction form come from the configured random stream, so a fixed syntactic rule does not cover every site.

The two arms deliberately present similar signed-shift shapes under different contexts. A learner that generalizes the trap while treating arithmetic right shift as logical right shift retains or flips `P` in the reconstructed value. A sound architecture-aware model still recovers the correct identity.

## ADC identity

With carry flag `CF = 1`, x86 ADC computes `x + C + 1`. Therefore:

```text
ADC(x, C) - C - 1 = x (mod 2^w)
```

One side-effecting inline-assembly primitive owns the full carry-dependent sequence: save flags, establish carry, run ADC and compensation, then restore flags. LLVM cannot schedule unrelated work inside that sequence.

## SBB identity

x86 SBB computes `x - C - CF`. With `CF = 1`:

```text
SBB(x, C) + C + 1 = x (mod 2^w)
```

SBB uses the same atomic flag sandbox and width-specific x86 lowering as ADC.

## RCR/RCL in paper mode

The paper presents:

```text
RCR(RCL(x, 1), 1)
```

and describes it as dependent on the initial carry. At a matching width, the two rotations are inverses on the combined `(CF, x)` state even when the initial carry is one. The pair is therefore not part of the verified carry-dependent argument.

The transform remains available as `paper-rcr-rcl` under `mode=paper` for comparison with the published construction. `verified` never selects it.

## Candidate coverage

The manager considers scalar `i32` and `i64` `add`, `sub`, `mul`, `and`, `or`, and `xor`. A particular transform may cover the operation itself or wrap one of its operands with an identity. The common eligibility check skips unsupported widths, vectors, dead/generated instructions, and arithmetic carrying `nsw` or `nuw`.

## Nesting and profiles

Profiles select a depth range, not a repetition count for one primitive. At each layer, the manager chooses among transforms allowed by the mode, opcode, and target. A²MBA-LLVM's selection probabilities control candidate density and the AAMBA/AGT mix; see [DESIGN.md](DESIGN.md).

Every new instruction receives `!a2mba.generated`. Because the worklist is complete before rewriting starts, generated IR cannot recursively expand in the same run.
