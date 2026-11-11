"""Prove the rotated addition rule before generating its LLVM IR builder."""

import argparse
import json
import re
from pathlib import Path

from hybrid_smt import check

ARGUMENTS = "a b sum shift back mask".split()
GUARD = "0 < shift < width and sum == a + b"
OPS = {
    "add": "Add",
    "sub": "Sub",
    "and": "And",
    "or": "Or",
    "xor": "Xor",
    "lshr": "LShr",
    "shl": "Shl",
}


def prove(steps, width, r):
    word = f"(_ BitVec {width})"

    def constant(value):
        return f"(_ bv{value} {width})"

    variables = ("a", "b")
    bindings = {
        "sum": "(bvadd a b)",
        "shift": constant(r),
        "back": constant(width - r),
        "mask": constant((1 << r) - 1),
    }
    expected = f"((_ rotate_left {r}) (bvadd ((_ rotate_right {r}) a) ((_ rotate_right {r}) b)))"
    query = ["(set-logic QF_BV)", "(set-option :timeout 5000)"]
    query.extend(f"(declare-const {v} {word})" for v in variables)
    for out, expression in bindings.items():
        query.append(f"(define-fun {out} () {word} {expression})")
    for out, op, lhs, rhs in steps:
        query.append(f"(define-fun {out} () {word} (bv{op} {lhs} {rhs}))")
    query += [f"(assert (distinct result {expected}))", "(check-sat)"]
    status = check("\n".join(query))
    if status != "unsat":
        raise ValueError(f"packedRotatedAdd, i{width}, rotation {r}: {status}")


def generate(spec):
    if spec["widths"] != [32, 64] or set(spec["rules"]) != {"packedRotatedAdd"}:
        raise ValueError("unexpected coordinate rule or widths")
    rule = spec["rules"]["packedRotatedAdd"]
    known = set(ARGUMENTS)
    if rule["requires"] != GUARD:
        raise ValueError("unrecognized packed addition precondition")
    steps = rule["steps"]
    for out, op, lhs, rhs in steps:
        if (
            not re.fullmatch(r"[a-z][a-zA-Z0-9]*", out)
            or out in known
            or op not in OPS
            or lhs not in known
            or rhs not in known
        ):
            raise ValueError(f"invalid coordinate step: {out}")
        known.add(out)
    if not steps or steps[-1][0] != "result":
        raise ValueError("missing result")
    for width in spec["widths"]:
        for r in range(1, width):
            prove(steps, width, r)
    parameters = ", ".join(f"llvm::Value *{a}" for a in ARGUMENTS)
    output = [
        f"llvm::Value *packedRotatedAdd(llvm::IRBuilderBase &builder, {parameters}) {{"
    ]
    for out, op, lhs, rhs in steps:
        output.append(
            f'  auto *{out} = builder.Create{OPS[op]}({lhs}, {rhs}, "a2mba.coord.{out}");'
        )
    output.extend(["  return result;", "}"])
    return "\n".join(output) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("spec", type=Path)
    parser.add_argument("header", type=Path)
    args = parser.parse_args()
    header = generate(json.loads(args.spec.read_text(encoding="utf-8")))
    args.header.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.header.with_suffix(".tmp")
    temporary.write_text(header, encoding="utf-8")
    temporary.replace(args.header)
