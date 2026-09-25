# Transforms

All identities below use `i32` or `i64` arithmetic modulo `2^w`. LLVM `APInt` handles constants and inverses at the selected width.

## Rule Explosion

Choose a nonzero odd constant `C` and its inverse `C_inv`:

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

Each instance gets a different `C` within the module. The addition form is Equation (7) in the authors' work. The subtraction and multiplication forms use the same scaling principle but were added here.

## Modular Scale

The identity:

```text
(x * C) * C_inv = x (mod 2^w)
```

can wrap an intermediate result after a primary transform. `ModularScale` is a local helper, not a separately named transform in the authors' work.

## Context Trap

For value `v`, the planner chooses `k` from 1 to 8 and a nonzero mask `Q` within the low `k` bits. Define:

```text
F = 2^(w-k-1)
P = 2^(w-k)
S = F - 1
M = P | Q
```

The trap arm clears bits that a shift could lose or sign-extend. The trigger arm forces the bit that will become the sign:

```text
low        = v & Q
high       = v & ~Q
trap       = ashr((v & S) << k, k) & M
trigger    = (ashr((v | F) << k, k) & M) ^ P
result     = high | ((trigger + trap) - low)
```

The trap evaluates to `low`. Before the XOR, the trigger evaluates to `low | P`; XOR removes `P`, leaving `low` there too. The reconstruction computes `(low + low) - low = low`, then joins it with `high`. Thus `result = v` for every input. The pass wraps the completed operation and does not assume a range for its operands.

This is one of six reconstruction forms. The others combine the equal `trap`, `trigger`, and `low` values with subtraction, XOR, OR, or disjoint addition. The random stream chooses `k`, `Q`, and the form.

The arms look similar but reach the signed shift under different contexts. A learner that applies a trap-derived rule to the trigger, or models arithmetic right shift as logical, can retain or flip `P` and get the wrong result. The reconstructed value then enters the nonlinear envelope with the original value and trigger projection as contexts. A model that handles the shifts and context correctly can recover the identity.

## Nonlinear envelope

The envelope builds two input-dependent factors, multiplies them, and makes an odd runtime key:

```text
p = factor0(x, y) * factor1(x, y)
K = 2*p + 1
result = (v * K) * inverse(K) mod 2^w
```

The random stream varies the factors, how `K` is formed, the inverse circuit, and multiplication order. The inverse uses Newton refinements or the finite product `product(1 + q^(2^i))`, where `q = 1 - K` is even and the exponents run from `1` to `w/2`. For the product form, `K * inverse(K) = 1 - q^w = 1 mod 2^w`. Both factors depend on program inputs, putting the emitted graph outside linear MBA. If an analyst can isolate and query the original inputs, black-box interpolation can still work; this change removes the old syntax-level shortcut only.

The envelope is still an identity. A nonlinear simplifier or symbolic engine can remove it by proving the inverse relation. The regression only checks that a fresh LLVM `default<O3>` pass does not restore the previous all-linear form.

## ADC identity

With carry flag `CF = 1`, x86 ADC computes `x + C + 1`. Therefore:

```text
ADC(x, C) - C - 1 = x (mod 2^w)
```

One side-effecting assembly block contains carry setup, ADC, and compensation. Carry can be set with `stc`, `clc` plus `cmc`, a bit test of a known odd constant, or compare plus complement. Compensation order varies. Each variant either saves/restores flags or declares them clobbered; LLVM cannot schedule work between these instructions.

## SBB identity

x86 SBB computes `x - C - CF`. With `CF = 1`:

```text
SBB(x, C) + C + 1 = x (mod 2^w)
```

SBB uses the same carry-setup choices, compensation variants, and `i32`/`i64` lowering as ADC.

## RCR/RCL in paper mode

The paper presents:

```text
RCR(RCL(x, 1), 1)
```

with an initial-carry precondition. At the same width, the two rotations undo each other on `(CF, x)` even if the initial carry is one. It does not demonstrate carry-dependent behavior in the way claimed there.

`mode=paper` retains `paper-rcr-rcl` for comparison. `verified` never selects it.

## Candidate coverage

Eligible source operations are scalar `i32` and `i64` `add`, `sub`, `mul`, `and`, `or`, and `xor`. Depending on the transform, the pass rewrites the operation or wraps an operand. It skips other widths, vectors, dead/generated instructions, and arithmetic with `nsw` or `nuw`.

## Hybrid expression search

Hybrid mode imports the selected operation into a bounded e-graph and applies fixed-width rules verified during the build. Extraction respects depth and AST limits. Its score prefers varied, alternating operators and penalizes cost and trivial spellings. That score steers search; it does not measure resilience.

`hybrid=ir` emits the DAG as LLVM IR. `hybrid=native` lowers it into straight-line x86-64 `mov`, `add`, `sub`, `imul`, `and`, `or`, `xor`, `not`, and `neg`. Scratch registers are early-clobber outputs, inputs are read-only, and flags are clobbered. The native block uses no memory, stack, branches, or hidden physical registers.

Both outputs get the nonlinear envelope. `hybrid-layers=none` adds nothing outside it. `profile` uses the active profile's Context Trap and architectural probabilities. `context-adc`, `context-sbb`, and `context-random` require Context Trap followed by the specified or seeded-random ADC/SBB wrapper. These layers run after equality search; the e-graph does not treat them as stateless algebraic rules.

SaMBA and asmMBA informed the search and native-lowering stages. The code and rule table here are separate, and their reported results are not reproduced.

## Stateful region encoding

`hybrid-region=stateful` protects a straight-line chain rather than each operation alone. Given result `v_i`, external context `c_i`, state `s_i`, and salt `t_i`, the next state is:

```text
s_(i+1) = (s_i xor v_i) * ((s_i xor c_i) or 1) + t_i  mod 2^w
```

The protected value then uses a state-dependent affine encoding:

```text
b_i = (s_(i+1) + a_i) * (left_i xor d_i)
k_i = 2 * ((s_(i+1) xor d_i) * (right_i + a_i)) + 1
e_i = (v_i + b_i) * k_i
v_i = e_i * inverse(k_i) - b_i  mod 2^w
```

`k_i` is odd, so its inverse exists modulo `2^w`. The next operation decodes the value inside the region, uses it to update state, and encodes its own result under a new key. Only the final decoded value reaches outside users.

The matcher requires at least two eligible operations in one basic block. The first uses inputs available before the region; each later one takes the previous result and another input available before the region. Loads and PHIs can supply those inputs. PHI exits, intermediate fan-out, and values defined partway through the chain are rejected.

The state is visible to an analyst. Per-operation two-input sampling no longer covers the whole chain, but a whole-region bit-vector analysis can still prove the transitions and decodes.

## Nesting and profiles

Profile depth does not repeat one primitive. Each layer chooses among transforms allowed for the mode, opcode, and target. Candidate and AAMBA/AGT selection probabilities are listed in [Design](DESIGN.md).

New instructions carry `!a2mba.generated`. The pass builds its worklist before rewriting, so new IR is not selected again in that run.
