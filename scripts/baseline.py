#!/usr/bin/env python3
"""Build the fixed A2MBA ablation matrix and export solver-ready expressions."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path
from typing import Sequence

import z3


@dataclasses.dataclass(frozen=True)
class Complexity:
    name: str
    level: str
    operations: int
    seed: int


@dataclasses.dataclass(frozen=True)
class Mode:
    name: str
    hybrid: str
    layers: str

    def options(self, complexity: Complexity, functions: str = "all") -> str:
        return (
            f"mode=verified;level={complexity.level};seed={complexity.seed};"
            f"functions={functions};hybrid={self.hybrid};hybrid-layers={self.layers};"
            "transform=auto;probability=100"
        )


COMPLEXITIES = (
    Complexity("basic", "light", 1, 1001),
    Complexity("light", "light", 2, 2001),
    Complexity("balanced", "balanced", 3, 3001),
    Complexity("medium", "medium", 5, 4001),
    Complexity("heavy", "heavy", 7, 5001),
)
MODES = (
    Mode("classic", "off", "none"),
    Mode("hybrid-ir", "ir", "none"),
    Mode("hybrid-native", "native", "none"),
    Mode("hybrid-layered", "ir", "context-random"),
)
OPERATORS = ("add", "xor", "sub", "or", "mul", "and")
SYMBOLS = {
    "add": "+",
    "sub": "-",
    "mul": "*",
    "and": "&",
    "or": "|",
    "xor": "^",
    "shl": "<<",
    "lshr": ">>",
    "ashr": ">>",
}


@dataclasses.dataclass(frozen=True)
class Expression:
    operator: str | None = None
    left: Expression | None = None
    right: Expression | None = None
    variable: str | None = None
    constant: int | None = None

    @staticmethod
    def var(name: str) -> Expression:
        return Expression(variable=name)

    @staticmethod
    def const(value: int) -> Expression:
        return Expression(constant=value)

    @staticmethod
    def unary(operator: str, operand: Expression) -> Expression:
        return Expression(operator=operator, left=operand)

    @staticmethod
    def binary(operator: str, left: Expression, right: Expression) -> Expression:
        return Expression(operator=operator, left=left, right=right)

    def render(self) -> str:
        if self.variable is not None:
            return self.variable
        if self.constant is not None:
            return f"({self.constant})" if self.constant < 0 else str(self.constant)
        assert self.operator is not None and self.left is not None
        if self.operator == "not":
            return f"(~{self.left.render()})"
        if self.operator == "neg":
            return f"(-{self.left.render()})"
        assert self.right is not None
        return f"({self.left.render()} {SYMBOLS[self.operator]} {self.right.render()})"

    def node_count(self) -> int:
        if self.variable is not None or self.constant is not None:
            return 1
        assert self.left is not None
        return (
            1 + self.left.node_count() + (self.right.node_count() if self.right else 0)
        )

    def variables(self) -> set[str]:
        if self.variable is not None:
            return {self.variable}
        if self.constant is not None:
            return set()
        assert self.left is not None
        return self.left.variables() | (self.right.variables() if self.right else set())

    def to_z3(self, variables: dict[str, z3.BitVecRef], width: int) -> z3.BitVecRef:
        if self.variable is not None:
            return variables[self.variable]
        if self.constant is not None:
            return z3.BitVecVal(self.constant, width)
        assert self.operator is not None and self.left is not None
        left = self.left.to_z3(variables, width)
        if self.operator == "not":
            return ~left
        if self.operator == "neg":
            return -left
        assert self.right is not None
        right = self.right.to_z3(variables, width)
        if self.operator == "add":
            return left + right
        if self.operator == "sub":
            return left - right
        if self.operator == "mul":
            return left * right
        if self.operator == "and":
            return left & right
        if self.operator == "or":
            return left | right
        if self.operator == "xor":
            return left ^ right
        if self.operator == "shl":
            return left << right
        if self.operator == "lshr":
            return z3.LShR(left, right)
        if self.operator == "ashr":
            return left >> right
        raise ValueError(f"unsupported operator: {self.operator}")


DEFINE_RE = re.compile(r"^define\s+i64\s+@([^ (]+)\(([^)]*)\).+\{$")
ARGUMENT_RE = re.compile(r"i64(?:\s+[-A-Za-z0-9().]+)*\s+%([-A-Za-z$._0-9]+)")
FREEZE_RE = re.compile(r"^\s*(%[-A-Za-z$._0-9]+)\s*=\s*freeze\s+i64\s+([^, ]+)")
BINARY_RE = re.compile(
    r"^\s*(%[-A-Za-z$._0-9]+)\s*=\s*"
    r"(add|sub|mul|and|or|xor|shl|lshr|ashr)\s+i64\s+([^,]+),\s+([^,!]+)"
)
EXTRACT_RE = re.compile(
    r"^\s*(%[-A-Za-z$._0-9]+)\s*=\s*extractvalue\s+.+\s+(%[-A-Za-z$._0-9]+),\s+(\d+)"
)
RETURN_RE = re.compile(r"^\s*ret\s+i64\s+([^, }]+)")
CALL_RE = re.compile(
    r'^\s*(%[-A-Za-z$._0-9]+)\s*=\s*call\s+.+?\s+asm(?:\s+sideeffect)?\s+"((?:\\.|[^"])*)",\s+"([^"]*)"\((.*)\)(?:,|$)'
)
REGISTER_RE = re.compile(r"\$\{(\d+):[kq]\}")


class BaselineError(RuntimeError):
    """The baseline could not be completed."""


def positive_integer(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("value must be positive")
    return number


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--opt", type=Path, required=True)
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("--llc", type=Path, required=True)
    parser.add_argument("--llvm-size", type=Path, required=True)
    parser.add_argument("--cvc5", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--per-level", type=positive_integer, default=20)
    parser.add_argument("--compile-runs", type=positive_integer, default=3)
    parser.add_argument("--warmups", type=positive_integer, default=2)
    parser.add_argument("--runtime-runs", type=positive_integer, default=15)
    parser.add_argument("--loop-iterations", type=positive_integer, default=5000)
    parser.add_argument("--z3-timeout-ms", type=positive_integer, default=5000)
    parser.add_argument("--cvc5-timeout-ms", type=positive_integer, default=10000)
    parser.add_argument("--performance-only", action="store_true")
    args = parser.parse_args()
    for name in ("opt", "clang", "llc", "llvm_size", "cvc5", "plugin"):
        path = getattr(args, name)
        if not path.is_file():
            parser.error(f"{name.replace('_', '-')} does not exist: {path}")
    if args.output.exists():
        parser.error(f"output already exists: {args.output}")
    if args.per_level > 200:
        parser.error("--per-level may not exceed 200")
    return args


def run(
    command: Sequence[str],
    *,
    environment: dict[str, str] | None = None,
    timeout: float | None = None,
) -> subprocess.CompletedProcess[bytes]:
    try:
        result = subprocess.run(
            command,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=environment,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise BaselineError(f"could not run {command[0]}: {error}") from error
    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace")
        raise BaselineError(
            f"command failed with status {result.returncode}: {subprocess.list2cmdline(command)}\n"
            f"{stderr}"
        )
    return result


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def make_module(
    per_level: int,
    *,
    runtime_iterations: int | None = None,
    source_chains: bool = True,
    complexities: Sequence[Complexity] = COMPLEXITIES,
) -> str:
    lines = [
        'source_filename = "a2mba-baseline"',
        'target triple = "x86_64-unknown-linux-gnu"',
        "",
        "@a2mba_sink = global i64 0",
        "",
    ]
    functions: list[str] = []
    for complexity in complexities:
        for index in range(per_level):
            name = f"target_{complexity.name}_{index:03d}"
            functions.append(name)
            lines.extend(
                (
                    f"define i64 @{name}(i64 %x, i64 %y, i64 %z, i64 %w) #0 {{",
                    "entry:",
                )
            )
            current = "%x"
            operands = (
                ("%y", "%z", "%w")
                if runtime_iterations is not None
                else ("%y", "%z", "%w", "%x")
            )
            operation_count = complexity.operations if source_chains else 1
            for step in range(operation_count):
                operator = OPERATORS[(index + step) % len(OPERATORS)]
                operand = operands[(index * 3 + step) % len(operands)]
                result = f"%v{step}"
                lines.append(f"  {result} = {operator} i64 {current}, {operand}")
                current = result
            lines.extend((f"  ret i64 {current}", "}", ""))

    if runtime_iterations is not None:
        lines.extend(
            ("define i32 @main() {", "entry:", "  br label %loop", "", "loop:")
        )
        lines.append("  %iteration = phi i64 [ 0, %entry ], [ %next, %loop ]")
        lines.append(
            "  %state = phi i64 [ 81985529216486895, %entry ], [ %state.next, %loop ]"
        )
        current = "%state"
        for index, function in enumerate(functions):
            result = f"%call{index}"
            lines.append(
                f"  {result} = call i64 @{function}(i64 {current}, i64 %iteration, "
                f"i64 {index + 3}, i64 {index * 17 + 5})"
            )
            current = result
        lines.extend(
            (
                f"  %state.next = xor i64 {current}, %iteration",
                "  %next = add nuw i64 %iteration, 1",
                f"  %done = icmp eq i64 %next, {runtime_iterations}",
                "  br i1 %done, label %exit, label %loop",
                "",
                "exit:",
                "  store volatile i64 %state.next, ptr @a2mba_sink",
                "  ret i32 0",
                "}",
                "",
            )
        )
    lines.append("attributes #0 = { noinline nounwind memory(none) willreturn }")
    lines.append("")
    return "\n".join(lines)


def decode_assembly(text: str) -> str:
    return re.sub(
        r"\\([0-9A-Fa-f]{2})", lambda match: chr(int(match.group(1), 16)), text
    )


def typed_operands(text: str, values: dict[str, Expression]) -> list[Expression]:
    result = []
    for token in re.findall(r"i64\s+([^, )]+)", text):
        result.append(resolve_operand(token, values))
    return result


def resolve_operand(token: str, values: dict[str, Expression]) -> Expression:
    token = token.strip()
    if token.startswith("%"):
        try:
            return values[token]
        except KeyError as error:
            raise ValueError(f"unknown SSA operand: {token}") from error
    return Expression.const(int(token, 0))


def assembly_operand(
    token: str,
    registers: list[Expression | None],
    inputs: list[Expression],
) -> Expression:
    register = REGISTER_RE.fullmatch(token.strip())
    if register:
        index = int(register.group(1))
        if index < len(registers):
            value = registers[index]
            if value is None:
                raise ValueError(f"native assembly reads undefined output {index}")
            return value
        input_index = index - len(registers)
        if input_index >= len(inputs):
            raise ValueError(f"native assembly references missing input {input_index}")
        return inputs[input_index]
    immediate = token.strip()
    if immediate.startswith("$$"):
        immediate = immediate[2:]
    return Expression.const(int(immediate, 0))


def lift_native_assembly(
    text: str, constraints: str, inputs: list[Expression]
) -> list[Expression]:
    scratch_count = 0
    for constraint in constraints.split(","):
        if constraint != "=&r":
            break
        scratch_count += 1
    if not scratch_count:
        raise ValueError("native assembly has no scratch outputs")
    registers: list[Expression | None] = [None] * scratch_count
    for raw_line in decode_assembly(text).splitlines():
        line = raw_line.strip()
        if not line:
            continue
        mnemonic, _, operand_text = line.partition(" ")
        operation = mnemonic[:-1] if mnemonic.endswith(("q", "l")) else mnemonic
        if operation in {"not", "neg"}:
            destination_match = REGISTER_RE.fullmatch(operand_text.strip())
            if not destination_match:
                raise ValueError(f"invalid unary assembly operand: {line}")
            destination = int(destination_match.group(1))
            current = registers[destination]
            if current is None:
                raise ValueError(
                    f"native assembly reads undefined output {destination}"
                )
            registers[destination] = Expression.unary(operation, current)
            continue
        source_text, separator, destination_text = operand_text.rpartition(", ")
        if not separator:
            raise ValueError(f"invalid binary assembly instruction: {line}")
        destination_match = REGISTER_RE.fullmatch(destination_text.strip())
        if not destination_match:
            raise ValueError(f"invalid assembly destination: {line}")
        destination = int(destination_match.group(1))
        source = assembly_operand(source_text, registers, inputs)
        if operation in {"mov", "movabs"}:
            registers[destination] = source
            continue
        current = registers[destination]
        if current is None:
            raise ValueError(f"native assembly reads undefined output {destination}")
        operator = "mul" if operation == "imul" else operation
        registers[destination] = Expression.binary(operator, current, source)
    if any(value is None for value in registers):
        raise ValueError("native assembly leaves an output undefined")
    return [value for value in registers if value is not None]


def parse_module(module_text: str) -> tuple[dict[str, Expression], Counter[str]]:
    functions: dict[str, Expression] = {}
    wrappers: Counter[str] = Counter()
    function_name: str | None = None
    values: dict[str, Expression] = {}
    tuples: dict[str, list[Expression]] = {}
    for raw_line in module_text.splitlines():
        line = raw_line.rstrip("\r")
        define = DEFINE_RE.match(line)
        if define:
            function_name = define.group(1)
            values = {
                f"%{name}": Expression.var(name)
                for name in ARGUMENT_RE.findall(define.group(2))
            }
            tuples = {}
            continue
        if function_name is None:
            continue
        freeze = FREEZE_RE.match(line)
        if freeze:
            name, operand = freeze.groups()
            values[name] = resolve_operand(operand, values)
            continue
        binary = BINARY_RE.match(line)
        if binary:
            name, operator, left, right = binary.groups()
            values[name] = Expression.binary(
                operator, resolve_operand(left, values), resolve_operand(right, values)
            )
            continue
        call = CALL_RE.match(line)
        if call:
            name, assembly, constraints, raw_inputs = call.groups()
            inputs = typed_operands(raw_inputs, values)
            if "pushfq" in assembly or ".adc" in name or ".sbb" in name:
                if not inputs:
                    raise ValueError("architectural wrapper has no value input")
                wrappers["adc" if ".adc" in name else "sbb"] += 1
                values[name] = inputs[0]
            else:
                wrappers["native"] += 1
                lifted = lift_native_assembly(assembly, constraints, inputs)
                if len(lifted) == 1:
                    values[name] = lifted[0]
                else:
                    tuples[name] = lifted
            continue
        extract = EXTRACT_RE.match(line)
        if extract:
            name, aggregate, raw_index = extract.groups()
            values[name] = tuples[aggregate][int(raw_index)]
            continue
        returned = RETURN_RE.match(line)
        if returned:
            functions[function_name] = resolve_operand(returned.group(1), values)
            continue
        if line == "}":
            function_name = None
            values = {}
            tuples = {}
    return functions, wrappers


def prove_with_cvc5(cvc5: Path, smt: str, timeout_ms: int) -> str:
    try:
        result = subprocess.run(
            [os.fspath(cvc5), "--lang=smt2", f"--tlimit-per={timeout_ms}"],
            input=smt.encode("utf-8"),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=timeout_ms / 1000 + 10,
        )
    except (OSError, subprocess.TimeoutExpired):
        return "unknown"
    if result.returncode != 0:
        return "unknown"
    answers = [
        line.strip()
        for line in result.stdout.decode("utf-8", errors="replace").splitlines()
        if line.strip() in {"sat", "unsat", "unknown"}
    ]
    return answers[-1] if answers else "unknown"


def prove_equivalent(
    original: Expression,
    transformed: Expression,
    description: str,
    cvc5: Path,
    z3_timeout_ms: int,
    cvc5_timeout_ms: int,
) -> str:
    names = sorted(original.variables() | transformed.variables())
    variables = {name: z3.BitVec(name, 64) for name in names}
    solver = z3.Solver()
    solver.set(timeout=z3_timeout_ms)
    solver.add(original.to_z3(variables, 64) != transformed.to_z3(variables, 64))
    result = solver.check()
    if result == z3.unsat:
        return "z3"
    if result == z3.sat:
        raise BaselineError(f"semantic counterexample for {description}")
    cvc5_result = prove_with_cvc5(
        cvc5,
        solver.sexpr() + "\n(check-sat)\n",
        cvc5_timeout_ms,
    )
    if cvc5_result == "unsat":
        return "cvc5"
    if cvc5_result == "sat":
        raise BaselineError(f"semantic counterexample for {description}")
    return "unknown"


def transform_module(
    args: argparse.Namespace,
    source: Path,
    destination: Path,
    options: str,
    *,
    optimize: bool,
) -> float:
    environment = os.environ.copy()
    environment["A2MBA_OPTIONS"] = options
    passes = "default<O3>" if optimize else "a2mba"
    command = [
        os.fspath(args.opt),
        f"-load-pass-plugin={args.plugin}",
        f"-passes={passes}",
        "-S",
        os.fspath(source),
        "-o",
        os.fspath(destination),
    ]
    started = time.perf_counter()
    run(command, environment=environment, timeout=600)
    return time.perf_counter() - started


def write_records(path: Path, records: list[dict[str, object]]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for record in records:
            stream.write(json.dumps(record, sort_keys=True) + "\n")


def export_corpora(args: argparse.Namespace, output: Path) -> dict[str, object]:
    corpus_root = output / "corpus"
    corpus_root.mkdir()
    mode_summary: dict[str, object] = {}
    for mode in MODES:
        mode_dir = corpus_root / mode.name
        mode_dir.mkdir()
        records: list[dict[str, object]] = []
        wrapper_counts: Counter[str] = Counter()
        for complexity in COMPLEXITIES:
            source_text = make_module(
                args.per_level,
                source_chains=False,
                complexities=(complexity,),
            )
            source_path = mode_dir / f"{complexity.name}-input.ll"
            transformed_path = mode_dir / f"{complexity.name}-transformed.ll"
            source_path.write_text(source_text, encoding="utf-8", newline="\n")
            options = mode.options(complexity)
            transform_module(
                args, source_path, transformed_path, options, optimize=False
            )
            original, _ = parse_module(source_text)
            transformed, wrappers = parse_module(
                transformed_path.read_text(encoding="utf-8")
            )
            wrapper_counts.update(wrappers)
            names = [
                f"target_{complexity.name}_{index:03d}"
                for index in range(args.per_level)
            ]
            if any(name not in original or name not in transformed for name in names):
                raise BaselineError(f"could not parse all {complexity.name} functions")
            for index, name in enumerate(names):
                before = original[name]
                after = transformed[name]
                oracle = prove_equivalent(
                    before,
                    after,
                    f"{mode.name}/{complexity.name}/{name}",
                    args.cvc5,
                    args.z3_timeout_ms,
                    args.cvc5_timeout_ms,
                )
                records.append(
                    {
                        "id": f"{mode.name}-{complexity.name}-{index:03d}",
                        "function": name,
                        "mode": mode.name,
                        "complexity": complexity.name,
                        "level": complexity.level,
                        "seed": complexity.seed,
                        "bit_width": 64,
                        "variables": sorted(before.variables()),
                        "original": before.render(),
                        "obfuscated": after.render(),
                        "original_nodes": before.node_count(),
                        "obfuscated_nodes": after.node_count(),
                        "growth_factor": round(
                            after.node_count() / before.node_count(), 4
                        ),
                        "changed": after.render() != before.render(),
                        "equivalence": "proved" if oracle != "unknown" else "unknown",
                        "oracle": oracle,
                        "projection": (
                            "native inline assembly lifted to its exact register expression"
                            if mode.hybrid == "native"
                            else "final LLVM integer expression; ADC/SBB modeled as its flag-aware identity"
                        ),
                    }
                )
        records_path = mode_dir / "expressions.jsonl"
        write_records(records_path, records)
        (mode_dir / "gamba.txt").write_text(
            "\n".join(str(record["obfuscated"]) for record in records) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        mode_summary[mode.name] = {
            "expressions": len(records),
            "changed": sum(bool(record["changed"]) for record in records),
            "unchanged": sum(not bool(record["changed"]) for record in records),
            "equivalence_proved": sum(
                record["equivalence"] == "proved" for record in records
            ),
            "equivalence_unknown": sum(
                record["equivalence"] == "unknown" for record in records
            ),
            "oracle_counts": dict(
                sorted(Counter(str(record["oracle"]) for record in records).items())
            ),
            "mean_original_nodes": statistics.fmean(
                int(record["original_nodes"]) for record in records
            ),
            "mean_obfuscated_nodes": statistics.fmean(
                int(record["obfuscated_nodes"]) for record in records
            ),
            "mean_growth_factor": statistics.fmean(
                float(record["growth_factor"]) for record in records
            ),
            "wrappers": dict(sorted(wrapper_counts.items())),
            "records": os.fspath(records_path.relative_to(output)),
        }
    return mode_summary


def text_size(tool: Path, executable: Path) -> int:
    output = run([os.fspath(tool), "-A", os.fspath(executable)]).stdout.decode(
        "utf-8", errors="replace"
    )
    for line in output.splitlines():
        fields = line.split()
        if fields[:1] == [".text"] and len(fields) >= 2:
            return int(fields[1])
    raise BaselineError(f"llvm-size did not report .text for {executable}")


def build_executable(
    args: argparse.Namespace,
    source: Path,
    output_ir: Path,
    output_object: Path,
    executable: Path,
    mode: Mode | None,
) -> float:
    started = time.perf_counter()
    if mode is None:
        run(
            [
                os.fspath(args.opt),
                "-passes=default<O3>",
                "-S",
                os.fspath(source),
                "-o",
                os.fspath(output_ir),
            ],
            timeout=600,
        )
    else:
        performance_complexity = Complexity("performance", "medium", 0, 0xA2BA5E)
        transform_module(
            args,
            source,
            output_ir,
            mode.options(performance_complexity, "regex:^target_.*$"),
            optimize=True,
        )
    run(
        [
            os.fspath(args.llc),
            "-O=3",
            "-filetype=obj",
            os.fspath(output_ir),
            "-o",
            os.fspath(output_object),
        ],
        timeout=600,
    )
    run(
        [os.fspath(args.clang), os.fspath(output_object), "-o", os.fspath(executable)],
        timeout=600,
    )
    return time.perf_counter() - started


def measure_performance(args: argparse.Namespace, output: Path) -> dict[str, object]:
    performance_dir = output / "performance"
    performance_dir.mkdir()
    source = performance_dir / "workload.ll"
    source.write_text(
        make_module(args.per_level, runtime_iterations=args.loop_iterations),
        encoding="utf-8",
        newline="\n",
    )
    variants: list[tuple[str, Mode | None]] = [
        ("plain", None),
        *[(mode.name, mode) for mode in MODES],
    ]
    build_results: dict[str, dict[str, object]] = {}
    executables: dict[str, Path] = {}
    for name, mode in variants:
        compile_times = []
        for run_index in range(args.compile_runs):
            stem = performance_dir / f"{name}-{run_index}"
            executable = stem.with_suffix(".exe")
            compile_times.append(
                build_executable(
                    args,
                    source,
                    stem.with_suffix(".ll"),
                    stem.with_suffix(".o"),
                    executable,
                    mode,
                )
            )
            executables[name] = executable
        final_executable = executables[name]
        build_results[name] = {
            "compile_seconds": compile_times,
            "compile_median_seconds": statistics.median(compile_times),
            "file_size_bytes": final_executable.stat().st_size,
            "text_size_bytes": text_size(args.llvm_size, final_executable),
            "sha256": sha256(final_executable),
        }

    outputs: dict[str, tuple[bytes, bytes]] = {}
    for name, _ in variants:
        result = run([os.fspath(executables[name])], timeout=120)
        outputs[name] = (result.stdout, result.stderr)
    if len(set(outputs.values())) != 1:
        raise BaselineError("benchmark variants produced different output")

    for warmup in range(args.warmups):
        for name, _ in (
            variants[warmup % len(variants) :] + variants[: warmup % len(variants)]
        ):
            run([os.fspath(executables[name])], timeout=120)

    runtime: dict[str, list[float]] = {name: [] for name, _ in variants}
    for run_index in range(args.runtime_runs):
        rotated = (
            variants[run_index % len(variants) :]
            + variants[: run_index % len(variants)]
        )
        for name, _ in rotated:
            started = time.perf_counter()
            run([os.fspath(executables[name])], timeout=120)
            runtime[name].append(time.perf_counter() - started)

    plain_compile = float(build_results["plain"]["compile_median_seconds"])
    plain_file = int(build_results["plain"]["file_size_bytes"])
    plain_text = int(build_results["plain"]["text_size_bytes"])
    plain_runtime = statistics.median(runtime["plain"])
    for name, _ in variants:
        result = build_results[name]
        median_runtime = statistics.median(runtime[name])
        result["runtime_seconds"] = runtime[name]
        result["runtime_median_seconds"] = median_runtime
        result["ratios_to_plain"] = {
            "compile_time": float(result["compile_median_seconds"]) / plain_compile,
            "file_size": int(result["file_size_bytes"]) / plain_file,
            "text_size": int(result["text_size_bytes"]) / plain_text,
            "runtime": median_runtime / plain_runtime,
        }
    return {
        "compile_runs": args.compile_runs,
        "warmups": args.warmups,
        "runtime_runs": args.runtime_runs,
        "loop_iterations": args.loop_iterations,
        "variants": build_results,
    }


def version(command: Path) -> str:
    result = run([os.fspath(command), "--version"], timeout=10)
    return result.stdout.decode("utf-8", errors="replace").splitlines()[0]


def main() -> int:
    args = parse_arguments()
    output = args.output.resolve()
    output.mkdir(parents=True)
    corpus = {} if args.performance_only else export_corpora(args, output)
    performance = measure_performance(args, output)
    report = {
        "schema": 1,
        "scope": "performance" if args.performance_only else "full",
        "matrix": [mode.name for mode in MODES],
        "per_level": args.per_level,
        "expression_count_per_mode": args.per_level * len(COMPLEXITIES),
        "oracle_timeouts_ms": {
            "z3": args.z3_timeout_ms,
            "cvc5": args.cvc5_timeout_ms,
        },
        "corpus": corpus,
        "performance": performance,
        "toolchain": {
            "opt": version(args.opt),
            "clang": version(args.clang),
            "llc": version(args.llc),
            "plugin_sha256": sha256(args.plugin),
        },
        "limits": [
            "ProMBA, CoBRA, and GAMBA consume exported integer expressions, not binaries.",
            "The native export is an exact lift of the generated register-only inline assembly.",
            "ADC/SBB wrappers are recorded and projected through their flag-aware identity.",
            "Unsupported syntax and solver timeouts must remain separate outcomes.",
        ],
    }
    (output / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(json.dumps({"output": os.fspath(output), "matrix": report["matrix"]}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (BaselineError, OSError, ValueError) as error:
        print(f"baseline: error: {error}", file=sys.stderr)
        raise SystemExit(1)
