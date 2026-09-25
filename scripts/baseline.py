#!/usr/bin/env python3
"""Build the fixed A2MBA ablation matrix and export solver-ready expressions."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import os
import random
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
    region: str = "none"

    def options(self, complexity: Complexity, functions: str = "all") -> str:
        return (
            f"mode=verified;level={complexity.level};seed={complexity.seed};"
            f"functions={functions};hybrid={self.hybrid};hybrid-region={self.region};"
            f"hybrid-layers={self.layers};"
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
    Mode("hybrid-stateful", "ir", "none", "stateful"),
)
PERFORMANCE_COMPLEXITY = Complexity("performance", "medium", 0, 0xA2BA5E)
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
        if operator in {"shl", "lshr", "ashr"} and (
            right.constant is None or not 0 <= right.constant < 64
        ):
            raise ValueError("the i64 oracle supports only constant shifts in [0, 63]")
        return Expression(operator=operator, left=left, right=right)

    def render(self, *, c_expression: bool = False) -> str:
        if self.variable is not None:
            return self.variable
        if self.constant is not None:
            if c_expression:
                return c_constant(self.constant)
            return f"({self.constant})" if self.constant < 0 else str(self.constant)
        assert self.operator is not None and self.left is not None
        left = self.left.render(c_expression=c_expression)
        if self.operator == "not":
            return f"(~{left})"
        if self.operator == "neg":
            return f"(-{left})"
        assert self.right is not None
        right = self.right.render(c_expression=c_expression)
        if self.operator == "ashr" and c_expression:
            return f"(uint64_t)((int64_t){left} >> {right})"
        return f"({left} {SYMBOLS[self.operator]} {right})"

    def node_count(self) -> int:
        return self._node_count(set())

    def _node_count(self, seen: set[int]) -> int:
        identity = id(self)
        if identity in seen:
            return 0
        seen.add(identity)
        if self.variable is not None or self.constant is not None:
            return 1
        assert self.left is not None
        return (
            1
            + self.left._node_count(seen)
            + (self.right._node_count(seen) if self.right else 0)
        )

    def expanded_node_count(self) -> int:
        return self._expanded_node_count({})

    def _expanded_node_count(self, cache: dict[int, int]) -> int:
        identity = id(self)
        if identity in cache:
            return cache[identity]
        count = 1
        if self.left is not None:
            count += self.left._expanded_node_count(cache)
        if self.right is not None:
            count += self.right._expanded_node_count(cache)
        cache[identity] = count
        return count

    def variables(self) -> set[str]:
        return self._variables({})

    def _variables(self, cache: dict[int, set[str]]) -> set[str]:
        identity = id(self)
        if identity in cache:
            return cache[identity]
        if self.variable is not None:
            result = {self.variable}
        elif self.constant is not None:
            result = set()
        else:
            assert self.left is not None
            result = self.left._variables(cache) | (
                self.right._variables(cache) if self.right else set()
            )
        cache[identity] = result
        return result

    def to_z3(self, variables: dict[str, z3.BitVecRef], width: int) -> z3.BitVecRef:
        return self._to_z3(variables, width, {})

    def _to_z3(
        self,
        variables: dict[str, z3.BitVecRef],
        width: int,
        cache: dict[int, z3.BitVecRef],
    ) -> z3.BitVecRef:
        identity = id(self)
        if identity in cache:
            return cache[identity]
        if self.variable is not None:
            result = variables[self.variable]
        elif self.constant is not None:
            result = z3.BitVecVal(self.constant, width)
        else:
            assert self.operator is not None and self.left is not None
            if self.operator in {"shl", "lshr", "ashr"} and (
                self.right is None
                or self.right.constant is None
                or not 0 <= self.right.constant < width
            ):
                raise ValueError(f"unsupported shift count at i{width}")
            left = self.left._to_z3(variables, width, cache)
            if self.operator == "not":
                result = ~left
            elif self.operator == "neg":
                result = -left
            else:
                assert self.right is not None
                right = self.right._to_z3(variables, width, cache)
                if self.operator == "add":
                    result = left + right
                elif self.operator == "sub":
                    result = left - right
                elif self.operator == "mul":
                    result = left * right
                elif self.operator == "and":
                    result = left & right
                elif self.operator == "or":
                    result = left | right
                elif self.operator == "xor":
                    result = left ^ right
                elif self.operator == "shl":
                    result = left << right
                elif self.operator == "lshr":
                    result = z3.LShR(left, right)
                elif self.operator == "ashr":
                    result = left >> right
                else:
                    raise ValueError(f"unsupported operator: {self.operator}")
        cache[identity] = result
        return result

    def evaluate(self, variables: dict[str, int], width: int) -> int:
        return self._evaluate(variables, width, {})

    def _evaluate(
        self, variables: dict[str, int], width: int, cache: dict[int, int]
    ) -> int:
        identity = id(self)
        if identity in cache:
            return cache[identity]
        mask = (1 << width) - 1
        if self.variable is not None:
            result = variables[self.variable] & mask
        elif self.constant is not None:
            result = self.constant & mask
        else:
            assert self.operator is not None and self.left is not None
            left = self.left._evaluate(variables, width, cache)
            if self.operator == "not":
                result = (~left) & mask
            elif self.operator == "neg":
                result = (-left) & mask
            else:
                assert self.right is not None
                right = self.right._evaluate(variables, width, cache)
                if self.operator == "add":
                    result = (left + right) & mask
                elif self.operator == "sub":
                    result = (left - right) & mask
                elif self.operator == "mul":
                    result = (left * right) & mask
                elif self.operator == "and":
                    result = left & right
                elif self.operator == "or":
                    result = left | right
                elif self.operator == "xor":
                    result = left ^ right
                else:
                    if right >= width:
                        raise ValueError(
                            f"shift count {right} produces poison at i{width}"
                        )
                    shift = right
                    if self.operator == "shl":
                        result = (left << shift) & mask
                    elif self.operator == "lshr":
                        result = left >> shift
                    elif self.operator == "ashr":
                        signed = (
                            left if left < 1 << (width - 1) else left - (1 << width)
                        )
                        result = (signed >> shift) & mask
                    else:
                        raise ValueError(f"unsupported operator: {self.operator}")
        cache[identity] = result
        return result


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


def c_constant(value: int, width: int = 64) -> str:
    return f"0x{value & ((1 << width) - 1):016x}ULL"


def normalize_flat_shifts(expression: Expression) -> Expression:
    cache: dict[int, Expression] = {}

    def lower(node: Expression) -> Expression:
        identity = id(node)
        if identity in cache:
            return cache[identity]
        if node.variable is not None or node.constant is not None:
            result = node
        else:
            assert node.operator is not None and node.left is not None
            left = lower(node.left)
            if node.right is None:
                result = Expression.unary(node.operator, left)
            else:
                right = lower(node.right)
                if node.operator == "ashr":
                    assert right.constant is not None
                    sign_bit = 1 << 63
                    biased = Expression.binary("xor", left, Expression.const(sign_bit))
                    shifted = Expression.binary("lshr", biased, right)
                    result = Expression.binary(
                        "sub", shifted, Expression.const(sign_bit >> right.constant)
                    )
                else:
                    result = Expression.binary(node.operator, left, right)
        cache[identity] = result
        return result

    return lower(expression)


def render_c_dag(expression: Expression) -> tuple[list[str], str]:
    statements: list[str] = []
    names: dict[int, str] = {}

    def lower(node: Expression) -> str:
        if node.variable is not None:
            return node.variable
        if node.constant is not None:
            return c_constant(node.constant)
        identity = id(node)
        if identity in names:
            return names[identity]
        assert node.operator is not None and node.left is not None
        left = lower(node.left)
        if node.operator == "not":
            value = f"~{left}"
        elif node.operator == "neg":
            value = f"-{left}"
        else:
            assert node.right is not None
            right = lower(node.right)
            if node.operator == "ashr":
                value = f"(uint64_t)((int64_t){left} >> {right})"
            else:
                value = f"{left} {SYMBOLS[node.operator]} {right}"
        name = f"a2mba_{len(statements)}"
        statements.append(f"  uint64_t {name} = {value};")
        names[identity] = name
        return name

    result = lower(expression)
    return statements, result


def write_promba_dag_source(path: Path, records: list[dict[str, object]]) -> None:
    lines = [
        "typedef unsigned long long uint64_t;",
        "typedef long long int64_t;",
        "",
    ]
    for record in records:
        dag = record.get("obfuscated_dag_c")
        if not isinstance(dag, dict):
            continue
        variables = ", ".join(
            f"uint64_t {variable}" for variable in record["variables"]
        )
        lines.extend((f"uint64_t {record['function']}({variables})", "{"))
        lines.extend(str(statement) for statement in dag["statements"])
        lines.extend((f"  return {dag['result']};", "}", ""))
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_promba_flat_source(path: Path, records: list[dict[str, object]]) -> None:
    lines = ["typedef unsigned long long uint64_t;", "typedef long long int64_t;", ""]
    for record in records:
        variables = ", ".join(
            f"uint64_t {variable}" for variable in record["variables"]
        )
        lines.extend(
            (
                f"uint64_t {record['function']}({variables})",
                "{",
                f"  return {record['obfuscated_c']};",
                "}",
                "",
            )
        )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def positive_integer(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("value must be positive")
    return number


def non_negative_integer(value: str) -> int:
    number = int(value)
    if number < 0:
        raise argparse.ArgumentTypeError("value must be non-negative")
    return number


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--opt", type=Path, required=True)
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("--llc", type=Path, required=True)
    parser.add_argument("--llvm-size", type=Path, required=True)
    parser.add_argument("--cvc5", type=Path, required=True)
    parser.add_argument("--plugin", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--modes",
        nargs="+",
        choices=tuple(mode.name for mode in MODES),
        default=tuple(mode.name for mode in MODES),
    )
    parser.add_argument("--per-level", type=positive_integer, default=20)
    parser.add_argument("--compile-runs", type=positive_integer, default=3)
    parser.add_argument("--warmups", type=positive_integer, default=2)
    parser.add_argument("--runtime-runs", type=positive_integer, default=15)
    parser.add_argument("--loop-iterations", type=positive_integer, default=5000)
    parser.add_argument("--z3-timeout-ms", type=positive_integer, default=5000)
    parser.add_argument("--cvc5-timeout-ms", type=positive_integer, default=10000)
    parser.add_argument("--max-flat-nodes", type=non_negative_integer, default=100000)
    parser.add_argument("--oracle", choices=("proof", "sampled"), default="proof")
    scope = parser.add_mutually_exclusive_group()
    scope.add_argument("--performance-only", action="store_true")
    scope.add_argument("--corpus-only", action="store_true")
    args = parser.parse_args()
    for name in ("opt", "clang", "llc", "llvm_size", "cvc5"):
        path = getattr(args, name)
        if not path.is_file():
            parser.error(f"{name.replace('_', '-')} does not exist: {path}")
    if args.plugin is not None and not args.plugin.is_file():
        parser.error(f"plugin does not exist: {args.plugin}")
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
    target_triple: str = "x86_64-unknown-linux-gnu",
) -> str:
    lines = [
        'source_filename = "a2mba-baseline"',
        f'target triple = "{target_triple}"',
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
        lines.extend(("declare i32 @putchar(i32)", ""))
        lines.extend(
            ("define i32 @main() {", "entry:", "  br label %loop", "", "loop:")
        )
        lines.append("  %iteration = phi i64 [ 0, %entry ], [ %next, %loop ]")
        lines.append(
            "  %state = phi i64 [ 81985529216486895, %entry ], [ %state.next, %loop ]"
        )
        lines.append(
            "  %checksum = phi i64 [ 1469598103934665603, %entry ], [ %checksum.next, %loop ]"
        )
        current = "%state"
        checksum = "%checksum"
        for index, function in enumerate(functions):
            result = f"%call{index}"
            lines.append(
                f"  {result} = call i64 @{function}(i64 {current}, i64 %iteration, "
                f"i64 {index + 3}, i64 {index * 17 + 5})"
            )
            lines.extend(
                (
                    f"  %checksum.mix{index} = xor i64 {checksum}, {result}",
                    f"  %checksum{index} = mul i64 %checksum.mix{index}, 1099511628211",
                )
            )
            current = result
            checksum = f"%checksum{index}"
        lines.extend(
            (
                f"  %state.next = xor i64 {current}, %iteration",
                f"  %checksum.next = xor i64 {checksum}, %iteration",
                "  %next = add nuw i64 %iteration, 1",
                f"  %done = icmp eq i64 %next, {runtime_iterations}",
                "  br i1 %done, label %exit, label %loop",
                "",
                "exit:",
                "  store volatile i64 %state.next, ptr @a2mba_sink",
                "  br label %print",
                "",
                "print:",
                "  %shift = phi i64 [ 60, %exit ], [ %shift.next, %print ]",
                "  %digit.wide = lshr i64 %checksum.next, %shift",
                "  %digit.low = trunc i64 %digit.wide to i32",
                "  %digit = and i32 %digit.low, 15",
                "  %decimal = icmp ult i32 %digit, 10",
                "  %decimal.char = add i32 %digit, 48",
                "  %hex.char = add i32 %digit, 87",
                "  %char = select i1 %decimal, i32 %decimal.char, i32 %hex.char",
                "  %written = call i32 @putchar(i32 %char)",
                "  %shift.next = sub i64 %shift, 4",
                "  %printed = icmp eq i64 %shift, 0",
                "  br i1 %printed, label %finish, label %print",
                "",
                "finish:",
                "  %newline = call i32 @putchar(i32 10)",
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
        if (
            mnemonic
            not in {
                "movq",
                "movabsq",
                "addq",
                "subq",
                "imulq",
                "andq",
                "orq",
                "xorq",
                "notq",
                "negq",
            }
            or ":k}" in operand_text
        ):
            raise ValueError(f"unsupported i64 native assembly instruction: {line}")
        operation = mnemonic[:-1]
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


def architectural_identity(
    assembly: str, constraints: str, inputs: list[Expression]
) -> str:
    if constraints != "=&r,0,r,~{memory},~{flags}" or len(inputs) != 2:
        raise ValueError("unsupported architectural wrapper contract")
    lines = [
        re.sub(r"\s+", " ", line.strip())
        for line in decode_assembly(assembly).splitlines()
        if line.strip()
    ]
    if lines[:1] == ["pushfq"] and lines[-1:] == ["popfq"]:
        lines = lines[1:-1]
    carry_setups = [
        ["stc"],
        ["clc", "cmc"],
        ["cmpq ${2:q}, ${2:q}", "cmc"],
    ]
    if inputs[1].constant is not None and inputs[1].constant & 1:
        carry_setups.append(["btq $$0, ${2:q}"])
    for carry in carry_setups:
        for family, operation, unit in (("adc", "sub", "dec"), ("sbb", "add", "inc")):
            body = [*carry, f"{family}q ${{2:q}}, ${{0:q}}"]
            source = f"{operation}q ${{2:q}}, ${{0:q}}"
            one = f"{operation}q $$1, ${{0:q}}"
            for compensation in (
                [source, one],
                [one, source],
                [source, f"{unit}q ${{0:q}}"],
            ):
                if lines == body + compensation:
                    return family
    raise ValueError("architectural wrapper does not match a supported identity")


def parse_module(module_text: str) -> tuple[dict[str, Expression], Counter[str]]:
    functions: dict[str, Expression] = {}
    wrappers: Counter[str] = Counter()
    function_name: str | None = None
    values: dict[str, Expression] = {}
    tuples: dict[str, list[Expression]] = {}
    seen_label = False
    returned_value = False
    for line_number, raw_line in enumerate(module_text.splitlines(), 1):
        line = raw_line.rstrip("\r")
        define = DEFINE_RE.match(line)
        if define:
            function_name = define.group(1)
            if function_name in functions:
                raise ValueError(f"duplicate function: {function_name}")
            values = {
                f"%{name}": Expression.var(name)
                for name in ARGUMENT_RE.findall(define.group(2))
            }
            tuples = {}
            seen_label = False
            returned_value = False
            continue
        if function_name is None:
            continue
        stripped = line.strip()
        if not stripped or stripped.startswith(";"):
            continue
        if stripped == "}":
            if not returned_value:
                raise ValueError(f"missing return in {function_name}")
            function_name = None
            values = {}
            tuples = {}
            continue
        if returned_value:
            raise ValueError(f"instructions after return in {function_name}")
        if re.fullmatch(r"[-A-Za-z$._0-9]+:\s*(?:;.*)?", stripped):
            if seen_label:
                raise ValueError(f"multiple basic blocks in {function_name}")
            seen_label = True
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
                family = architectural_identity(assembly, constraints, inputs)
                wrappers[family] += 1
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
            returned_value = True
            continue
        raise ValueError(
            f"unsupported IR in {function_name} at line {line_number}: {stripped}"
        )
    if function_name is not None:
        raise ValueError(f"unterminated function: {function_name}")
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


def sample_equivalent(
    original: Expression,
    transformed: Expression,
    description: str,
    width: int = 64,
) -> str:
    names = sorted(original.variables() | transformed.variables())
    digest = hashlib.sha256(description.encode("utf-8")).digest()
    generator = random.Random(int.from_bytes(digest[:8], "little"))
    mask = (1 << width) - 1
    vectors = [
        {name: 0 for name in names},
        {name: mask for name in names},
        {name: 1 << (width - 1) for name in names},
    ]
    vectors.extend(
        {name: generator.getrandbits(width) for name in names} for _ in range(64)
    )
    for vector in vectors:
        if original.evaluate(vector, width) != transformed.evaluate(vector, width):
            raise BaselineError(f"semantic counterexample for {description}: {vector}")
    return "sampled"


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
    ]
    if args.plugin is not None:
        command.append(f"-load-pass-plugin={args.plugin}")
    command.extend(
        [
            f"-passes={passes}",
            "-S",
            os.fspath(source),
            "-o",
            os.fspath(destination),
        ]
    )
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
    for mode in (mode for mode in MODES if mode.name in args.modes):
        mode_dir = corpus_root / mode.name
        mode_dir.mkdir()
        records: list[dict[str, object]] = []
        flat_records: list[dict[str, object]] = []
        wrapper_counts: Counter[str] = Counter()
        flat_skip_reasons: Counter[str] = Counter()
        for complexity in COMPLEXITIES:
            emitted_complexity = (
                dataclasses.replace(
                    complexity, operations=max(2, complexity.operations)
                )
                if mode.region == "stateful"
                else complexity
            )
            source_text = make_module(
                args.per_level,
                source_chains=mode.region == "stateful",
                complexities=(emitted_complexity,),
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
                description = f"{mode.name}/{complexity.name}/{name}"
                oracle = (
                    prove_equivalent(
                        before,
                        after,
                        description,
                        args.cvc5,
                        args.z3_timeout_ms,
                        args.cvc5_timeout_ms,
                    )
                    if args.oracle == "proof"
                    else sample_equivalent(before, after, description)
                )
                expanded_nodes = after.expanded_node_count()
                flat_expression = normalize_flat_shifts(after)
                flat_expanded_nodes = flat_expression.expanded_node_count()
                dag_statements, dag_result = render_c_dag(after)
                record: dict[str, object] = {
                    "id": f"{mode.name}-{complexity.name}-{index:03d}",
                    "function": name,
                    "mode": mode.name,
                    "complexity": complexity.name,
                    "level": complexity.level,
                    "seed": complexity.seed,
                    "bit_width": 64,
                    "variables": sorted(before.variables() | after.variables()),
                    "original": before.render(),
                    "original_nodes": before.node_count(),
                    "obfuscated_nodes": after.node_count(),
                    "growth_factor": round(after.node_count() / before.node_count(), 4),
                    "changed": (dag_statements, dag_result) != render_c_dag(before),
                    "equivalence": (
                        "proved"
                        if oracle in {"z3", "cvc5"}
                        else "sampled" if oracle == "sampled" else "unknown"
                    ),
                    "oracle": oracle,
                    "projection": (
                        "native inline assembly lifted to its exact register expression"
                        if mode.hybrid == "native"
                        else (
                            "complete stateful LLVM region expression"
                            if mode.region == "stateful"
                            else "final LLVM integer expression; ADC/SBB modeled as its flag-aware identity"
                        )
                    ),
                }
                record["obfuscated_dag_c"] = {
                    "statements": dag_statements,
                    "result": dag_result,
                }
                record["expanded_nodes"] = expanded_nodes
                record["flat_expanded_nodes"] = flat_expanded_nodes
                if args.max_flat_nodes and flat_expanded_nodes <= args.max_flat_nodes:
                    record["obfuscated"] = flat_expression.render()
                    record["obfuscated_c"] = after.render(c_expression=True)
                    record["flat_exported"] = True
                    flat_records.append(record)
                else:
                    record["flat_exported"] = False
                    reason = (
                        "disabled"
                        if not args.max_flat_nodes
                        else "expanded-node-count-exceeds-limit"
                    )
                    record["flat_skip_reason"] = reason
                    flat_skip_reasons[reason] += 1
                records.append(record)
        records_path = mode_dir / "expressions.jsonl"
        write_records(records_path, records)
        write_promba_dag_source(mode_dir / "promba.c", records)
        flat_records_path = mode_dir / "flat-expressions.jsonl"
        write_records(flat_records_path, flat_records)
        write_promba_flat_source(mode_dir / "promba-flat.c", flat_records)
        for tool in ("gamba", "cobra"):
            (mode_dir / f"{tool}.txt").write_text(
                "\n".join(str(record["obfuscated"]) for record in flat_records) + "\n",
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
            "equivalence_sampled": sum(
                record["equivalence"] == "sampled" for record in records
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
            "minimum_expanded_nodes": min(
                int(record["expanded_nodes"]) for record in records
            ),
            "flat_expression_exported": bool(flat_records),
            "flat_expression_count": len(flat_records),
            "flat_expression_limit_nodes": args.max_flat_nodes,
            "flat_expression_skipped": dict(sorted(flat_skip_reasons.items())),
            "wrappers": dict(sorted(wrapper_counts.items())),
            "records": os.fspath(records_path.relative_to(output)),
            "flat_records": os.fspath(flat_records_path.relative_to(output)),
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
        environment = os.environ.copy()
        environment["A2MBA_OPTIONS"] = "functions=regex:^$"
        run(
            [
                os.fspath(args.opt),
                "-passes=default<O3>",
                "-S",
                os.fspath(source),
                "-o",
                os.fspath(output_ir),
            ],
            environment=environment,
            timeout=600,
        )
    else:
        transform_module(
            args,
            source,
            output_ir,
            mode.options(PERFORMANCE_COMPLEXITY, "regex:^target_.*$"),
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


def run_workload(executable: Path, expected: bytes | None = None) -> bytes:
    result = run([os.fspath(executable)], timeout=120)
    if not re.fullmatch(rb"[0-9a-f]{16}\r?\n", result.stdout) or result.stderr:
        raise BaselineError(f"benchmark did not produce a checksum: {executable}")
    if expected is not None and result.stdout != expected:
        raise BaselineError(f"benchmark checksum differs from plain: {executable}")
    return result.stdout


def measure_performance(args: argparse.Namespace, output: Path) -> dict[str, object]:
    performance_dir = output / "performance"
    performance_dir.mkdir()
    target = run([os.fspath(args.clang), "-print-target-triple"], timeout=10)
    target_triple = target.stdout.decode("utf-8").strip()
    if not re.fullmatch(r"x86_64-[A-Za-z0-9_.-]+", target_triple) or not any(
        system in target_triple for system in ("-windows", "-linux")
    ):
        raise BaselineError(f"unsupported benchmark target: {target_triple!r}")
    source = performance_dir / "workload.ll"
    source.write_text(
        make_module(
            args.per_level,
            runtime_iterations=args.loop_iterations,
            target_triple=target_triple,
        ),
        encoding="utf-8",
        newline="\n",
    )
    variants: list[tuple[str, Mode | None]] = [
        ("plain", None),
        *[(mode.name, mode) for mode in MODES if mode.name in args.modes],
    ]
    build_results: dict[str, dict[str, object]] = {}
    executables: dict[str, Path] = {}
    compile_times: dict[str, list[float]] = {name: [] for name, _ in variants}
    for run_index in range(args.compile_runs):
        rotated = (
            variants[run_index % len(variants) :]
            + variants[: run_index % len(variants)]
        )
        for name, mode in rotated:
            stem = performance_dir / f"{name}-{run_index}"
            executable = stem.with_suffix(".exe")
            compile_times[name].append(
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
    for name, mode in variants:
        final_executable = executables[name]
        build_results[name] = {
            "options": (
                mode.options(PERFORMANCE_COMPLEXITY, "regex:^target_.*$")
                if mode is not None
                else "functions=regex:^$"
            ),
            "compile_seconds": compile_times[name],
            "compile_median_seconds": statistics.median(compile_times[name]),
            "file_size_bytes": final_executable.stat().st_size,
            "text_size_bytes": text_size(args.llvm_size, final_executable),
            "sha256": sha256(final_executable),
        }

    expected = run_workload(executables["plain"])
    for name, _ in variants:
        run_workload(executables[name], expected)

    for warmup in range(args.warmups):
        for name, _ in (
            variants[warmup % len(variants) :] + variants[: warmup % len(variants)]
        ):
            run_workload(executables[name], expected)

    runtime: dict[str, list[float]] = {name: [] for name, _ in variants}
    for run_index in range(args.runtime_runs):
        rotated = (
            variants[run_index % len(variants) :]
            + variants[: run_index % len(variants)]
        )
        for name, _ in rotated:
            started = time.perf_counter()
            run_workload(executables[name], expected)
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
        "target_triple": target_triple,
        "workload_sha256": sha256(source),
        "checksum": expected.decode("ascii").strip(),
        "timing": "whole-process wall time, including startup and checksum output",
        "compile_runs": args.compile_runs,
        "warmups": args.warmups,
        "runtime_runs": args.runtime_runs,
        "loop_iterations": args.loop_iterations,
        "variants": build_results,
    }


def version(command: Path) -> str:
    result = run([os.fspath(command), "--version"], timeout=10)
    lines = result.stdout.decode("utf-8", errors="replace").strip().splitlines()
    if not lines:
        raise BaselineError(f"{command} returned no version information")
    return next((line.strip() for line in lines if "version" in line.lower()), lines[0])


def main() -> int:
    args = parse_arguments()
    output = args.output.resolve()
    output.mkdir(parents=True)
    corpus = {} if args.performance_only else export_corpora(args, output)
    performance = {} if args.corpus_only else measure_performance(args, output)
    report = {
        "schema": 1,
        "scope": (
            "performance"
            if args.performance_only
            else "corpus" if args.corpus_only else "full"
        ),
        "matrix": list(args.modes),
        "per_level": args.per_level,
        "expression_count_per_mode": args.per_level * len(COMPLEXITIES),
        "oracle_timeouts_ms": {
            "z3": args.z3_timeout_ms,
            "cvc5": args.cvc5_timeout_ms,
        },
        "oracle_mode": args.oracle,
        "corpus": corpus,
        "performance": performance,
        "toolchain": {
            "opt": version(args.opt),
            "clang": version(args.clang),
            "llc": version(args.llc),
            "z3": z3.get_version_string(),
            "cvc5": version(args.cvc5),
            "harness_sha256": sha256(Path(__file__)),
            "pass_entry_sha256": sha256(args.plugin or args.opt),
            "pass_entry": "plugin" if args.plugin is not None else "standalone driver",
        },
        "limits": [
            "ProMBA, CoBRA, and GAMBA consume exported single-return expressions, not binaries or DAG locals.",
            "Stateful DAG C output is a readable projection and is not counted as an external-tool expression unless it also has a bounded flat export.",
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
