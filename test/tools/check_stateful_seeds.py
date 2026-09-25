import argparse
import hashlib
import json
import operator
import os
from pathlib import Path
import random
import re
import subprocess

OPERATIONS = {
    "add": operator.add,
    "sub": operator.sub,
    "mul": operator.mul,
    "and": operator.and_,
    "or": operator.or_,
    "xor": operator.xor,
}
PROFILES = (
    ("light", 0),
    ("balanced", 17),
    ("medium", (1 << 63) + 1),
    ("heavy", (1 << 64) - 1),
)


def make_cases():
    random_source = random.Random(0xA2BA0919)
    opcodes = tuple(OPERATIONS)
    cases = []
    for width in (32, 64):
        mask = (1 << width) - 1
        operands = [f"a{index}" for index in range(8)] + [
            0,
            1,
            2,
            3,
            mask,
            1 << (width - 1),
            (1 << (width - 1)) - 1,
            0xAAAAAAAAAAAAAAAA & mask,
            0x5555555555555555 & mask,
        ]
        for index in range(12):
            steps = [
                (
                    opcodes[(index + step) % len(opcodes)],
                    random_source.choice(operands),
                    bool(random_source.getrandbits(1)),
                )
                for step in range(2 + index % 6)
            ]
            cases.append((width, f"candidate_{width}_{index}", steps))
    return cases


def make_vectors():
    mask = (1 << 64) - 1
    edges = (
        0,
        1,
        mask,
        1 << 63,
        (1 << 63) - 1,
        1 << 31,
        (1 << 32) - 1,
        0xAAAAAAAAAAAAAAAA,
    )
    vectors = [tuple([edge] * 8) for edge in edges]
    vectors += [
        tuple(edges[(index + offset) % len(edges)] for offset in range(8))
        for index in range(len(edges))
    ]
    random_source = random.Random(0x519EED)
    vectors += [
        tuple(random_source.getrandbits(64) for _ in range(8)) for _ in range(256)
    ]
    return vectors


def evaluate(case, arguments):
    width, _, steps = case
    mask = (1 << width) - 1
    value = arguments[0] & mask
    for opcode, operand, reversed_operands in steps:
        external = arguments[int(operand[1:])] if isinstance(operand, str) else operand
        external &= mask
        left, right = (external, value) if reversed_operands else (value, external)
        value = OPERATIONS[opcode](left, right) & mask
    return value


def make_module(cases, target, *, corrupt=False):
    lines = [f'target triple = "{target}"', ""]
    for index, (width, name, steps) in enumerate(cases):
        arguments = ", ".join(f"i{width} %a{argument}" for argument in range(8))
        lines += [f"define i{width} @{name}({arguments}) noinline {{", "entry:"]
        value = "%a0"
        for step, (opcode, operand, reversed_operands) in enumerate(steps):
            external = f"%{operand}" if isinstance(operand, str) else str(operand)
            left, right = (external, value) if reversed_operands else (value, external)
            result = f"%s{step}"
            lines.append(f"  {result} = {opcode} i{width} {left}, {right}")
            value = result
        if corrupt and index == 0:
            lines.append(f"  %wrong = xor i{width} {value}, 1")
            value = "%wrong"
        lines += [f"  ret i{width} {value}", "}", ""]
    return "\n".join(lines)


def make_host(cases, vectors):
    lines = ["#include <stdint.h>", "#include <stdio.h>", ""]
    for width, name, _ in cases:
        arguments = ", ".join([f"uint{width}_t"] * 8)
        lines.append(f"uint{width}_t {name}({arguments});")
    lines += ["", "static const uint64_t inputs[][8] = {"]
    for arguments in vectors:
        lines.append(
            "  {" + ", ".join(f"UINT64_C({value})" for value in arguments) + "},"
        )
    lines += ["};", f"static const uint64_t expected[][{len(cases)}] = {{"]
    for arguments in vectors:
        values = [evaluate(case, arguments) for case in cases]
        lines.append("  {" + ", ".join(f"UINT64_C({value})" for value in values) + "},")
    lines += [
        "};",
        "",
        "int main(void) {",
        f"  for (unsigned i = 0; i < {len(vectors)}; ++i) {{",
        "    const uint64_t *a = inputs[i];",
        "    uint64_t actual;",
    ]
    for index, (width, name, _) in enumerate(cases):
        arguments = ", ".join(f"(uint{width}_t)a[{argument}]" for argument in range(8))
        lines += [
            f"    actual = {name}({arguments});",
            f"    if (actual != expected[i][{index}]) {{",
            f'      fprintf(stderr, "{name}, vector %u: expected %016llx, got %016llx\\n",',
            f"              i, (unsigned long long)expected[i][{index}],",
            "              (unsigned long long)actual);",
            "      return 1;",
            "    }",
        ]
    lines += [
        "  }",
        f'  puts("PASS: {len(cases) * len(vectors)} comparisons");',
        "  return 0;",
        "}",
        "",
    ]
    return "\n".join(lines)


def run(command, environment):
    result = subprocess.run(
        [str(argument) for argument in command],
        env=environment,
        capture_output=True,
        text=True,
        errors="replace",
        timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {command}\n{result.stderr}"
        )
    return result


def build_and_check(
    clang, source, host, executable, optimization, environment, expected
):
    run([clang, optimization, source, host, "-o", executable], environment)
    result = run([executable], environment)
    if result.stdout.strip() != expected or result.stderr:
        raise RuntimeError(
            f"unexpected oracle output: {executable}\n{result.stdout}\n{result.stderr}"
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--opt", required=True, nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.opt:
        parser.error("--opt needs an executable and optional plugin arguments")
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["A2MBA_OPTIONS"] = "functions=regex:^$"
    target = run([args.clang, "-print-target-triple"], environment).stdout.strip()
    if not re.fullmatch(r"x86_64-[A-Za-z0-9_.-]+", target):
        raise RuntimeError(f"expected an x86-64 target, got {target!r}")
    cases = make_cases()
    vectors = make_vectors()
    source_text = make_module(cases, target)
    source = root / "source.ll"
    source.write_text(source_text, encoding="utf-8")
    host = root / "host.c"
    host.write_text(make_host(cases, vectors), encoding="utf-8")
    host_object = root / "host.o"
    run([args.clang, "-O3", "-c", host, "-o", host_object], environment)
    expected = f"PASS: {len(cases) * len(vectors)} comparisons"
    for optimization in ("-O0", "-O3"):
        build_and_check(
            args.clang,
            source,
            host_object,
            root / f"plain{optimization}.exe",
            optimization,
            environment,
            expected,
        )

    broken_source = root / "broken.ll"
    broken_source.write_text(make_module(cases, target, corrupt=True), encoding="utf-8")
    broken_executable = root / "broken.exe"
    run(
        [args.clang, "-O3", broken_source, host_object, "-o", broken_executable],
        environment,
    )
    broken = subprocess.run(
        [str(broken_executable)],
        env=environment,
        capture_output=True,
        text=True,
        errors="replace",
        timeout=30,
    )
    if broken.returncode != 1 or "candidate_32_0, vector 0:" not in broken.stderr:
        raise RuntimeError("the oracle did not reject the deliberately wrong result")

    results = []
    operation_count = sum(len(steps) for _, _, steps in cases)
    for level, seed in PROFILES:
        for emitter in ("ir", "native"):
            for layers in ("none", "context-random"):
                label = f"{level}-{seed}-{emitter}-{layers}"
                protected = root / f"{label}.ll"
                options = (
                    f"mode=verified;level={level};seed={seed};functions=all;"
                    f"hybrid={emitter};hybrid-region=stateful;hybrid-layers={layers};"
                    "probability=100;stats=true"
                )
                protected_environment = dict(environment, A2MBA_OPTIONS=options)
                transformed = run(
                    [*args.opt, "-passes=a2mba,verify", "-S", source, "-o", protected],
                    protected_environment,
                )
                regions = re.search(r"stateful regions: (\d+)", transformed.stderr)
                emitter_name = "IR" if emitter == "ir" else "native"
                operations = re.search(
                    rf"hybrid {emitter_name}: (\d+)", transformed.stderr
                )
                if (
                    regions is None
                    or int(regions[1]) != len(cases)
                    or operations is None
                    or int(operations[1]) != operation_count
                ):
                    raise RuntimeError(
                        f"incomplete stateful coverage for {label}:\n{transformed.stderr}"
                    )
                for optimization in ("-O0", "-O3"):
                    executable = root / f"{label}{optimization}.exe"
                    build_and_check(
                        args.clang,
                        protected,
                        host_object,
                        executable,
                        optimization,
                        environment,
                        expected,
                    )
                results.append(
                    {
                        "options": options,
                        "regions": int(regions[1]),
                        "operations": int(operations[1]),
                        "comparisons_per_executable": len(cases) * len(vectors),
                        "optimization_levels": ["O0", "O3"],
                    }
                )
                print(f"PASS: {label}, {len(cases)} regions at O0/O3", flush=True)
    report = {
        "target": target,
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "functions": len(cases),
        "vectors": len(vectors),
        "results": results,
        "incorrect_result_rejected": True,
    }
    (root / "report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    print(
        f"PASS: {len(results) * 2} protected executables, "
        f"{len(results) * 2 * len(cases) * len(vectors)} comparisons"
    )


if __name__ == "__main__":
    main()
