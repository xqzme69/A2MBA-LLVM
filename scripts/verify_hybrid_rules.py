#!/usr/bin/env python3
"""Verify the MBA rule table and generate its C++ representation."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import time

from hybrid_smt import check, version

OP_ARITY = {
    "add": 2,
    "sub": 2,
    "mul": 2,
    "and": 2,
    "or": 2,
    "xor": 2,
    "not": 1,
    "neg": 1,
}
CPP_OP_NAME = {operation: operation.capitalize() for operation in OP_ARITY}
SMT_OP_NAME = {operation: "bv" + operation for operation in OP_ARITY}
METAVARIABLES = {"$x": 0, "$y": 1, "$z": 2}


def validate_pattern(pattern: object, depth: int = 0) -> set[str]:
    if depth > 16:
        raise ValueError("pattern depth >16")
    if isinstance(pattern, str) and pattern in METAVARIABLES:
        return {pattern}
    if type(pattern) is int and 0 <= pattern <= 0xFFFFFFFF:
        return set()
    if (
        not isinstance(pattern, list)
        or not pattern
        or pattern[0] not in OP_ARITY
        or len(pattern) != OP_ARITY[pattern[0]] + 1
    ):
        raise ValueError(f"invalid pattern node: {pattern!r}")
    metavariables = set()
    for child in pattern[1:]:
        metavariables |= validate_pattern(child, depth + 1)
    return metavariables


def render_smt(pattern: object, width: int) -> str:
    if isinstance(pattern, str):
        return pattern[1:]
    if type(pattern) is int:
        return f"(_ bv{pattern % (1 << width)} {width})"
    operation, *operands = pattern
    rendered_operands = " ".join(render_smt(operand, width) for operand in operands)
    return f"({SMT_OP_NAME[operation]} {rendered_operands})"


def build_query(rule: dict[str, object], width: int) -> str:
    declarations = "\n".join(
        f"(declare-fun {variable} () (_ BitVec {width}))"
        for variable in ("x", "y", "z")
    )
    return (
        f"(set-logic QF_BV)\n(set-option :timeout 5000)\n{declarations}\n"
        f'(assert (not (= {render_smt(rule["lhs"], width)} '
        f'{render_smt(rule["rhs"], width)})))\n'
        "(check-sat-using (then (using-params simplify :som true) smt))\n"
    )


def render_cpp_pattern(pattern: object) -> str:
    generated_nodes = []

    def append_node(node: object) -> int:
        if isinstance(node, str):
            generated_node = (
                f"{{Op::Input, invalidId, invalidId, {METAVARIABLES[node]}}}"
            )
        elif type(node) is int:
            generated_node = f"{{Op::Constant, invalidId, invalidId, UINT64_C({node})}}"
        else:
            child_ids = [append_node(child) for child in node[1:]]
            right_child = str(child_ids[1]) if len(child_ids) > 1 else "invalidId"
            generated_node = (
                f"{{Op::{CPP_OP_NAME[node[0]]}, {child_ids[0]}, {right_child}, 0}}"
            )
        node_id = len(generated_nodes)
        generated_nodes.append(generated_node)
        return node_id

    root_id = append_node(pattern)
    return "Pattern{{" + ", ".join(generated_nodes) + "}, " + str(root_id) + "}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    arguments = parser.parse_args()
    raw_spec = arguments.spec.read_bytes()
    specification = json.loads(raw_spec)
    if specification.get("schema") != 1 or specification.get("widths") != [
        8,
        16,
        32,
        64,
    ]:
        raise ValueError("expected schema 1 and widths [8,16,32,64]")
    seen_names = set()
    verification_records = []
    generated_rules = []
    for rule in specification["rules"]:
        name = rule["name"]
        if type(rule.get("negation_family", False)) is not bool:
            raise ValueError("negation_family must be a bool")
        if not re.fullmatch(r"[a-z0-9_]+", name) or name in seen_names:
            raise ValueError(f"invalid/duplicate name {name}")
        seen_names.add(name)
        left_metavariables = validate_pattern(rule["lhs"])
        right_metavariables = validate_pattern(rule["rhs"])
        if not right_metavariables.issubset(left_metavariables):
            raise ValueError(f"{name}: RHS uses unbound metavariable")
        for width in specification["widths"]:
            query = build_query(rule, width)
            started_at = time.monotonic()
            result = check(query)
            verification_records.append(
                {
                    "rule": name,
                    "width": width,
                    "result": result,
                    "query_sha256": hashlib.sha256(query.encode()).hexdigest(),
                    "seconds": round(time.monotonic() - started_at, 6),
                }
            )
            if result != "unsat":
                raise RuntimeError(
                    f"{name}/i{width}: {result}; rule table NOT generated"
                )
        generated_rules.append(
            '    Rule{"'
            + name
            + '", '
            + str(rule.get("negation_family", False)).lower()
            + ", "
            + render_cpp_pattern(rule["lhs"])
            + ", "
            + render_cpp_pattern(rule["rhs"])
            + "}"
        )
    specification_digest = hashlib.sha256(raw_spec).hexdigest()
    generated_header = (
        "namespace a2mba::hybrid_core::detail {\nconst std::vector<Rule> &rules() {\n"
        "  static const std::vector<Rule> table{\n"
        + ",\n".join(generated_rules)
        + "\n  };\n  return table;\n}\n}\nnamespace a2mba::hybrid_core {\n"
        f'const char *rulesFingerprint() noexcept {{ return "{specification_digest}"; }}\n}}\n'
    )
    verification_report = {
        "scope": "Rule identities only; not proof of the e-graph, LLVM pass or x86 backend",
        "z3": version(),
        "spec_sha256": specification_digest,
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "rules": len(generated_rules),
        "queries": verification_records,
    }
    for output_path, content in (
        (arguments.header, generated_header),
        (arguments.report, json.dumps(verification_report, indent=2) + "\n"),
    ):
        output_path.parent.mkdir(parents=True, exist_ok=True)
        temporary_path = output_path.with_suffix(output_path.suffix + ".tmp")
        temporary_path.write_text(content, encoding="utf-8")
        temporary_path.replace(output_path)
    print(
        f"Verified {len(generated_rules)} rules / {len(verification_records)} "
        f"width-specific queries; SHA256 {specification_digest}"
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Rule verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
