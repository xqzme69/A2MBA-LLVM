import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import z3

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

import baseline  # noqa: E402


def module(body):
    return f"define i64 @target(i64 %x, i64 %y) {{\nentry:\n{body}\n}}\n"


def wrapper(assembly, constant=3):
    return module(
        f'  %a2mba.adc = call i64 asm sideeffect "{assembly}", '
        f'"=&r,0,r,~{{memory}},~{{flags}}"(i64 %x, i64 {constant})\n'
        "  ret i64 %a2mba.adc"
    )


class BaselineParserTests(unittest.TestCase):
    def test_modular_arithmetic(self):
        functions, _ = baseline.parse_module(
            module("  %sum = add i64 %x, %y, !a2mba.generated !0\n  ret i64 %sum")
        )
        self.assertEqual(
            functions["target"].evaluate({"x": (1 << 64) - 1, "y": 1}, 64), 0
        )

    def test_freeze_preserves_concrete_inputs(self):
        functions, _ = baseline.parse_module(
            module(
                "  %stable = freeze i64 %x\n"
                "  %doubled = add i64 %stable, %stable\n"
                "  ret i64 %doubled"
            )
        )
        for value in (0, 1, (1 << 63) - 1, (1 << 63), (1 << 64) - 1):
            with self.subTest(value=value):
                self.assertEqual(
                    functions["target"].evaluate({"x": value, "y": 0}, 64),
                    (2 * value) & ((1 << 64) - 1),
                )

    def test_concrete_oracle_rejects_undef_and_poison(self):
        for value in ("undef", "poison"):
            for instruction in (
                f"%result = freeze i64 {value}",
                f"%result = and i64 {value}, 0",
            ):
                with self.subTest(instruction=instruction), self.assertRaises(
                    ValueError
                ):
                    baseline.parse_module(module(f"  {instruction}\n  ret i64 %result"))

    def test_unsupported_ir_is_rejected_even_when_return_does_not_use_it(self):
        for instruction in (
            "store volatile i64 %x, ptr null",
            "call void @side_effect()",
            "%unused = add nsw i64 %x, %y",
            "%unused = load i64, ptr null",
            "br label %next",
        ):
            with self.subTest(instruction=instruction), self.assertRaisesRegex(
                ValueError, "unsupported IR"
            ):
                baseline.parse_module(module(f"  {instruction}\n  ret i64 %x"))

    def test_incomplete_or_multiple_block_functions_are_rejected(self):
        for text in (
            module("  %sum = add i64 %x, %y"),
            module("next:\n  ret i64 %x"),
            module("  ret i64 %x\n  ret i64 %y"),
            module("  ret i64 %x").removesuffix("}\n"),
        ):
            with self.subTest(text=text), self.assertRaises(ValueError):
                baseline.parse_module(text)

    def test_shift_counts_are_not_reduced_modulo_width(self):
        for operator in ("shl", "lshr", "ashr"):
            for amount in ("64", "-1", "%y"):
                with self.subTest(operator=operator, amount=amount), self.assertRaises(
                    ValueError
                ):
                    baseline.parse_module(
                        module(
                            f"  %shift = {operator} i64 %x, {amount}\n  ret i64 %shift"
                        )
                    )
            expression = baseline.Expression.binary(
                operator, baseline.Expression.var("x"), baseline.Expression.const(32)
            )
            with self.assertRaises(ValueError):
                expression.evaluate({"x": 1}, 32)
            with self.assertRaises(ValueError):
                expression.to_z3({"x": z3.BitVec("x", 32)}, 32)

    def test_defined_shifts_match_bitvector_semantics(self):
        for operator in ("shl", "lshr", "ashr"):
            for amount in (0, 1, 63):
                expression = baseline.Expression.binary(
                    operator,
                    baseline.Expression.var("x"),
                    baseline.Expression.const(amount),
                )
                for value in (0, 1, 1 << 63, (1 << 64) - 1):
                    with self.subTest(operator=operator, amount=amount, value=value):
                        expected = z3.simplify(
                            expression.to_z3({"x": z3.BitVecVal(value, 64)}, 64)
                        ).as_long()
                        self.assertEqual(
                            expression.evaluate({"x": value}, 64), expected
                        )

    def test_c_export_preserves_signed_shift_and_unsigned_constants(self):
        expression = baseline.Expression.binary(
            "ashr", baseline.Expression.var("x"), baseline.Expression.const(3)
        )
        self.assertIn("(int64_t)x", expression.render(c_expression=True))
        self.assertEqual(
            baseline.Expression.const(-1).render(c_expression=True),
            "0xffffffffffffffffULL",
        )

    def test_flat_shift_normalization_preserves_all_i64_inputs(self):
        variable = z3.BitVec("x", 64)
        for amount in range(64):
            expression = baseline.Expression.binary(
                "ashr", baseline.Expression.var("x"), baseline.Expression.const(amount)
            )
            normalized = baseline.normalize_flat_shifts(expression)
            solver = z3.Solver()
            solver.set(timeout=1000)
            solver.add(
                expression.to_z3({"x": variable}, 64)
                != normalized.to_z3({"x": variable}, 64)
            )
            with self.subTest(amount=amount):
                self.assertEqual(solver.check(), z3.unsat)

    def test_architectural_identity_is_checked_instead_of_trusting_its_name(self):
        correct = r"pushfq\0Astc\0Aadcq ${2:q}, ${0:q}\0Asubq ${2:q}, ${0:q}\0Asubq $$1, ${0:q}\0Apopfq"
        functions, wrappers = baseline.parse_module(wrapper(correct))
        self.assertEqual(wrappers["adc"], 1)
        self.assertEqual(functions["target"].evaluate({"x": 17}, 64), 17)
        for broken in (
            correct.replace("subq $$1", "subq $$2"),
            correct.replace("stc", "clc"),
            correct.replace(r"\0Apopfq", ""),
            "addq $$1, ${0:q}",
        ):
            with self.subTest(assembly=broken), self.assertRaises(ValueError):
                baseline.parse_module(wrapper(broken))

    def test_bit_test_carry_requires_an_odd_constant(self):
        assembly = (
            r"btq $$0, ${2:q}\0Aadcq ${2:q}, ${0:q}\0Asubq ${2:q}, ${0:q}\0Adecq ${0:q}"
        )
        baseline.parse_module(wrapper(assembly, 3))
        with self.assertRaises(ValueError):
            baseline.parse_module(wrapper(assembly, 2))

    def test_i32_native_instructions_are_not_lifted_as_i64(self):
        with self.assertRaises(ValueError):
            baseline.lift_native_assembly(
                "movl ${1:k}, ${0:k}", "=&r,r", [baseline.Expression.var("x")]
            )


class BaselineExportTests(unittest.TestCase):
    def export_fixture(self, mode, limit, rewrite, verify):
        args = argparse.Namespace(
            modes=[mode], per_level=1, oracle="sampled", max_flat_nodes=limit
        )

        def transform(args, source, destination, options, *, optimize):
            destination.write_text(
                rewrite(source.read_text(encoding="utf-8")), encoding="utf-8"
            )

        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            with patch.object(
                baseline, "COMPLEXITIES", baseline.COMPLEXITIES[:1]
            ), patch.object(baseline, "transform_module", side_effect=transform):
                summary = baseline.export_corpora(args, output)[mode]
            directory = output / "corpus" / mode
            record = json.loads((directory / "expressions.jsonl").read_text())
            verify(summary, record, directory)

    def test_expansion_limit_applies_to_every_mode(self):
        def expand_zero(text):
            lines = []
            for line in text.splitlines():
                if line.startswith("  ret i64 "):
                    original = line.removeprefix("  ret i64 ")
                    lines.append("  %zero0 = sub i64 %x, %x")
                    for index in range(1, 19):
                        previous = f"%zero{index - 1}"
                        lines.append(f"  %zero{index} = add i64 {previous}, {previous}")
                    lines.append(f"  %answer = add i64 {original}, %zero18")
                    line = "  ret i64 %answer"
                lines.append(line)
            return "\n".join(lines) + "\n"

        def verify(summary, record, directory):
            self.assertEqual(summary["flat_expression_count"], 0)
            self.assertEqual(
                record["flat_skip_reason"], "expanded-node-count-exceeds-limit"
            )
            self.assertEqual(record["equivalence"], "sampled")
            self.assertNotIn("obfuscated", record)
            self.assertTrue(record["obfuscated_dag_c"]["statements"])
            self.assertEqual((directory / "gamba.txt").read_text().strip(), "")
            self.assertEqual((directory / "cobra.txt").read_text().strip(), "")

        for mode in baseline.MODES:
            with self.subTest(mode=mode.name):
                self.export_fixture(mode.name, 100, expand_zero, verify)

    def test_small_expression_exports_at_exact_limit(self):
        def verify(summary, record, directory):
            self.assertEqual(summary["flat_expression_count"], 1)
            self.assertEqual(record["expanded_nodes"], 3)
            self.assertFalse(record["changed"])
            self.assertIn(
                record["obfuscated_c"], (directory / "promba-flat.c").read_text()
            )

        self.export_fixture("hybrid-ir", 3, lambda text: text, verify)

    def test_zero_limit_disables_flat_export(self):
        def verify(summary, record, directory):
            self.assertEqual(summary["flat_expression_count"], 0)
            self.assertEqual(record["flat_skip_reason"], "disabled")

        self.export_fixture("hybrid-ir", 0, lambda text: text, verify)

    def test_equal_node_count_does_not_hide_a_rewrite(self):
        def verify(summary, record, directory):
            self.assertEqual(record["original_nodes"], record["obfuscated_nodes"])
            self.assertTrue(record["changed"])
            self.assertEqual(record["equivalence"], "sampled")

        self.export_fixture(
            "hybrid-stateful",
            100,
            lambda text: text.replace("add i64 %x, %y", "add i64 %y, %x"),
            verify,
        )


class BaselinePerformanceTests(unittest.TestCase):
    def test_workload_uses_the_requested_target(self):
        text = baseline.make_module(
            1, runtime_iterations=2, target_triple="x86_64-pc-windows-msvc"
        )
        self.assertIn('target triple = "x86_64-pc-windows-msvc"', text)
        self.assertNotIn("x86_64-unknown-linux-gnu", text)

    def test_workload_requires_a_checksum(self):
        for output in (b"", b"0000\n", b"not a checksum\n"):
            with self.subTest(output=output), patch.object(
                baseline,
                "run",
                return_value=subprocess.CompletedProcess([], 0, output, b""),
            ), self.assertRaises(baseline.BaselineError):
                baseline.run_workload(Path("workload.exe"))

    def test_workload_checks_each_result(self):
        expected = b"0123456789abcdef\r\n"
        with patch.object(
            baseline,
            "run",
            return_value=subprocess.CompletedProcess([], 0, expected, b""),
        ):
            self.assertEqual(baseline.run_workload(Path("workload.exe")), expected)
            baseline.run_workload(Path("workload.exe"), expected)
            with self.assertRaisesRegex(baseline.BaselineError, "checksum"):
                baseline.run_workload(Path("workload.exe"), b"0000000000000000\r\n")
        with patch.object(
            baseline,
            "run",
            return_value=subprocess.CompletedProcess([], 0, expected, b"error"),
        ), self.assertRaises(baseline.BaselineError):
            baseline.run_workload(Path("workload.exe"), expected)

    def test_plain_build_does_not_inherit_protection_options(self):
        args = argparse.Namespace(opt=Path("opt"), llc=Path("llc"), clang=Path("clang"))
        with patch.dict(
            baseline.os.environ, {"A2MBA_OPTIONS": "functions=all"}
        ), patch.object(baseline, "run") as run:
            baseline.build_executable(
                args,
                Path("input.ll"),
                Path("output.ll"),
                Path("output.o"),
                Path("plain.exe"),
                None,
            )
        environment = run.call_args_list[0].kwargs["environment"]
        self.assertEqual(environment["A2MBA_OPTIONS"], "functions=regex:^$")


if __name__ == "__main__":
    unittest.main()
