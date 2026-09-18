# Benchmarking

## Hybrid baseline

The snapshot recorded on 2026-09-18 compares four LLVM 21.1.8 configurations:

| Name | Configuration |
| --- | --- |
| classic | `hybrid=off;hybrid-layers=none` |
| hybrid IR | `hybrid=ir;hybrid-layers=none` |
| hybrid native | `hybrid=native;hybrid-layers=none` |
| hybrid layered | `hybrid=ir;hybrid-layers=context-random` |

Each mode received the same 100 single-operation `i64` inputs: 20 per complexity preset. Complexity controls the selected protection profile and hybrid search budget rather than the size of the source expression. A site is counted as changed only when the final exported expression differs from its source.

| Mode | Changed | Full-width proof | Unknown | Mean nodes after | Mean growth |
| --- | ---: | ---: | ---: | ---: | ---: |
| classic | 46/100 | 100 | 0 | 17.14 | 5.71x |
| hybrid IR | 82/100 | 95 | 5 | 51.64 | 17.21x |
| hybrid native | 58/100 | 100 | 0 | 24.55 | 8.18x |
| hybrid layered | 83/100 | 90 | 10 | 129.64 | 43.21x |

Z3 received five seconds per expression; cvc5 received ten seconds when Z3 returned unknown. An unknown result is not treated as an equivalence proof. The executable workload also required all protected variants to produce exactly the same output as the unprotected build.

### External simplifiers

The table below counts only sites that A²MBA actually changed. A correct reduction is smaller than the exported protected expression and equivalent to the original under an independent full-width oracle. A deceptive result is smaller but differs from the original. CoBRA's own `--verify` result is not trusted for that distinction.

| Mode | GAMBA | CoBRA | ProMBA fixed sample |
| --- | --- | --- | --- |
| classic | 33 correct, 13 unsupported | 33 correct, 8 deceptive, 5 unsupported | 0/20 reduced |
| hybrid IR | 82 correct | 63 correct, 19 unknown | 0/20 reduced |
| hybrid native | 58 correct | 53 correct, 5 unknown | 0/20 reduced |
| hybrid layered | 1 correct, 82 unsupported | 1 correct, 28 deceptive, 6 unchanged, 14 unknown, 26 unsupported, 4 timeout, 4 failed | 0/20 reduced |

ProMBA used the first four expressions from each of the five complexity groups, for 20 expressions per mode. All 80 outputs were unchanged. The number of changed expressions inside those samples was 9 for classic, 17 for hybrid IR, 11 for hybrid native, and 16 for hybrid layered.

The clean hybrid IR and native modes are ablations, not the strongest configuration. Their algebraic payloads were usually reduced by GAMBA and CoBRA. The layered configuration is materially different, but its 82 GAMBA parser rejections are a compatibility boundary rather than evidence of resilience. CoBRA produced one independently confirmed reduction and 28 independently rejected reductions on its 83 changed layered inputs.

### Compile time, size, and runtime

The performance workload contains 100 dependency-preserving functions and runs the chain 250,000 times. Five complete builds were measured; runtime uses three warmups followed by 15 rotated runs. File size is the complete ELF executable, while `.text` is reported separately.

| Mode | Compile | File size | `.text` | Runtime |
| --- | ---: | ---: | ---: | ---: |
| plain | 0.344 s / 1.00x | 24,144 B / 1.00x | 4,329 B / 1.00x | 0.0406 s / 1.00x |
| classic | 0.599 s / 1.74x | 85,584 B / 3.54x | 67,497 B / 15.59x | 3.224 s / 79.39x |
| hybrid IR | 4.058 s / 11.79x | 40,528 B / 1.68x | 20,633 B / 4.77x | 0.1568 s / 3.86x |
| hybrid native | 3.999 s / 11.62x | 48,720 B / 2.02x | 28,601 B / 6.61x | 0.1759 s / 4.33x |
| hybrid layered | 4.532 s / 13.16x | 65,104 B / 2.70x | 46,729 B / 10.79x | 0.5261 s / 12.96x |

These ratios describe this synthetic workload on one Ryzen 5 5600G under WSL2. They are useful for comparing the four configurations under one setup, not for predicting application-wide overhead.

The machine-readable [result manifest](results/hybrid-baseline-2026-09-18.json) records the complete aggregate outcomes, upstream revisions, artifact hashes, environment, and interpretation limits.

The corpus and performance workload can be regenerated with:

```bash
python scripts/baseline.py \
  --opt /path/to/opt-21 \
  --clang /path/to/clang-21 \
  --llc /path/to/llc-21 \
  --llvm-size /path/to/llvm-size-21 \
  --cvc5 /path/to/cvc5 \
  --plugin /path/to/A2MBA.so \
  --output /path/to/results \
  --per-level 20 \
  --compile-runs 5 \
  --warmups 3 \
  --runtime-runs 15 \
  --loop-iterations 250000
```

`--performance-only` skips corpus export and the per-expression equivalence checks when only the workload measurement is needed.

## Randomized Context Trap evaluation

The snapshot below was recorded on 2026-08-22 with LLVM 21.1.8 and an A²MBA plugin build. The harness generated 1,000 expressions: 500 over `i32` and 500 over `i64`. It forced one Context Trap layer per expression so the result measures that transform rather than a mixed profile.

| Setting | Value |
| --- | --- |
| mode | `verified` |
| level | `heavy` |
| transform | `context-trap` |
| probability | `100` |
| depth | `1` |
| seed base | `2725928960` |

An independent full-width bit-vector check proved the original and obfuscated expression equivalent in all 1,000 cases before solver outcomes were classified. The corpus contained 878 distinct obfuscated expressions and 566 distinct parameter sets.

### ProMBA

ProMBA returned a deceptive simplification for 870 expressions and reached its top-level timeout on 130. It returned no correct simplification. The Deception Success Rate is therefore 100% over 870 eligible expressions. As in the paper, top-level timeouts are reported separately rather than added to the DSR denominator.

![ProMBA outcomes: 870 deceptive simplifications and 130 top-level timeouts](results/promba-outcomes.png)

The six reconstruction variants were all exercised. Their group sizes differ because selection was randomized; bar length encodes the actual number of expressions in each group.

![ProMBA outcomes split across six reconstruction variants](results/promba-variants.png)

### CoBRA and GAMBA

The same 1,000-expression corpus was also passed to CoBRA and GAMBA. CoBRA accepted a deceptive simplification in 690 cases, rejected 305 inputs as unsupported, and produced five unknown results caused by its internal Z3 timeout. It did not crash. For the 690 deceptive cases, CoBRA's own model accepted the simplification as equivalent while an independent full-width oracle found a counterexample to that simplification.

GAMBA rejected all 1,000 inputs at the parser boundary because this corpus uses arithmetic right shift. That is a compatibility result, not evidence that the expressions resisted GAMBA, so it is excluded from resilience claims.

![ProMBA, CoBRA, and GAMBA outcomes on the same corpus](results/tool-outcomes.png)

### Parameter-space check

A separate exhaustive Z3 check covered all 6,024 supported combinations of shift, mask, and reconstruction form. It proved the physical identity in every case and found a counterexample to the intentionally incomplete logical model in every case. There were no verification failures.

![Context Trap parameter-space verification: 6,024 of 6,024 cases](results/context-trap-parameter-space.png)

These results cover a forced, depth-one Context Trap experiment. They do not measure the complete `heavy` profile, the architectural transforms, runtime overhead, code size, or resistance to manual analysis. They also do not reproduce the paper's corpus or establish a direct paper-versus-project comparison.

The machine-readable [result manifest](results/context-trap-1000.json) records the configuration, aggregate counts, tool artifact hashes, and SHA-256 hashes of the full local reports. The per-expression traces and external evaluation harness are not part of this repository, so this snapshot is documentation rather than a CI gate.

## Published prototype results

The paper reports these overhead factors for its LLVM 15 prototype:

| Paper level | Runtime factor | Code-size factor |
| --- | ---: | ---: |
| Light | 1.8x | 2.1x |
| Medium | 3.0x | 3.5x |
| Heavy | 5.5x | 6.3x |

These figures belong to the authors' LLVM 15 prototype. A²MBA-LLVM differs in its LLVM version, implementation, presets, corpus, and environment. The Context Trap snapshot above is separate and does not make these figures transferable.

## Local runtime and size measurement

`scripts/benchmark.py` builds the same source twice with one LLVM 21 Clang and one argument set:

1. protected, through `a2mba-clang`;
2. baseline, through plain Clang.

It alternates the two executables, compares stdout and stderr, records whole-process wall time, then reports median runtime and file-size ratios:

```bash
python scripts/benchmark.py \
  --clang clang-21 \
  --plugin build/lib/A2MBA.so \
  --level balanced \
  --seed 1337 \
  --iterations 20 \
  --warmups 3 \
  --json results/verify-balanced.json \
  path/to/workload.c -- -DNDEBUG
```

PowerShell uses the same arguments with a `.dll` plugin. Values beginning with `-` for a program argument should use the `--run-arg=value` form before the source path.

The default is `functions=all`; otherwise an unannotated standalone workload could pass through untouched. Use `--functions annotated` when the source includes `sdk/a2mba.h` and the annotated boundary is part of the experiment.

The script refuses compiler flags that change the output mode/path and refuses to overwrite retained binaries or a JSON result. Compilation and program execution use argument arrays with no command shell.

### Interpretation limits

Whole-process timing includes startup, so short workloads mostly measure process launch. The reported size is the complete executable rather than only `.text`; those are different measurements and should be identified as such.

The output comparison assumes a deterministic workload. If the program prints timestamps, random values, addresses, or internal timing, make its observable output deterministic before benchmarking rather than disabling the correctness check.

## Determinism and diversity

`scripts/diversity.py` works on object files so linker variability stays out of the result. It compiles the base seed twice and requires identical SHA-256 hashes. It then tries consecutive seeds and requires every resulting object hash to differ:

```bash
python scripts/diversity.py \
  --clang clang-21 \
  --plugin build/lib/A2MBA.so \
  --level balanced \
  --base-seed 1000 \
  --variants 100 \
  path/to/workload.c -- -DNDEBUG
```

For that workload and toolchain, the run establishes only same-seed object determinism and observed different-seed hash diversity. It says nothing about entropy, clustering resistance, or semantic diversity. A collision can also mean that no eligible operation was selected, so the hash alone does not identify the cause.

Use `--output-dir` only when the object variants are needed for later inspection. Without it, temporary objects are removed.

## Combined validation

For a quick build/test plus a small local sample experiment:

```bash
python scripts/validate.py \
  --llvm-dir /path/to/llvm-21/lib/cmake/llvm \
  --sample path/to/workload.c \
  --diversity-variants 4 \
  --benchmark-iterations 3
```

Without `--sample`, `validate.py` configures and builds the project, runs CTest, then invokes `a2mba-clang --doctor`. Any failed stage stops the run.

## Reproduction data

A reproducible report includes:

- repository revision;
- complete Clang/LLVM version and target triple;
- plugin build type and sanitizer status;
- operating system, CPU, power profile, and core pinning policy;
- source workload revision and input;
- exact compiler arguments and `A2MBA_OPTIONS`;
- warmup and measured iteration counts;
- raw per-run times, medians, and binary sizes;
- whether outputs and exit codes matched;
- object hashes and seeds for diversity runs.

Baseline/protected pairs need repeated runs because a single ratio says nothing about variance. Excluding a run requires a rule chosen before measurement. Performance, size, correctness, and resilience remain separate results.

## Running another resilience experiment

`scripts/baseline.py` generates the four fixed corpora and performs the local equivalence, compile-time, size, and runtime checks. GAMBA, ProMBA, CoBRA, MBA-Blast, and symbolic-execution tools are not bundled. Repeating the external-tool stage requires pinned revisions, time and memory limits, and an independent semantic oracle. The repository publishes aggregate results and hashes rather than the external harness or raw corpus.
