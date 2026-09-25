import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


def load_script(name):
    path = Path(__file__).resolve().parents[1] / "scripts" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(f"a2mba_{name}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


benchmark = load_script("benchmark")
diversity = load_script("diversity")
validate = load_script("validate")


class ValidationCliTests(unittest.TestCase):
    def test_default_region_remains_none(self):
        for module in (benchmark, diversity, validate):
            with self.subTest(script=module.__name__):
                arguments = [] if module is validate else ["sample.c"]
                parsed = module.build_parser().parse_args(arguments)
                self.assertEqual(parsed.hybrid_region, "none")

    def test_stateful_region_is_accepted(self):
        for module in (benchmark, diversity, validate):
            with self.subTest(script=module.__name__):
                arguments = ["--hybrid", "ir", "--hybrid-region", "stateful"]
                if module is not validate:
                    arguments.append("sample.c")
                parsed = module.build_parser().parse_args(arguments)
                self.assertEqual(parsed.hybrid_region, "stateful")

    def test_unknown_region_is_rejected(self):
        for module in (benchmark, diversity, validate):
            with self.subTest(script=module.__name__):
                parser = module.build_parser()
                with patch.object(parser, "error", side_effect=ValueError) as error:
                    with self.assertRaises(ValueError):
                        parser.parse_args(["--hybrid-region", "unknown"])
                self.assertIn("invalid choice", error.call_args.args[0])

    def test_benchmark_forwards_and_records_region(self):
        with tempfile.TemporaryDirectory(prefix="a2mba-cli-test-") as temporary:
            root = Path(temporary)
            source = root / "sample.c"
            source.write_text("int main(void) { return 0; }\n", encoding="utf-8")
            report = root / "report.json"
            with patch.object(
                benchmark, "find_clang", return_value=root / "clang"
            ), patch.object(
                benchmark,
                "compile_program",
                side_effect=lambda command, output: output.write_bytes(b"executable"),
            ) as compile_program, patch.object(
                benchmark, "run_pair", return_value=(1.0, 2.0)
            ), patch(
                "builtins.print"
            ):
                status = benchmark.main(
                    [
                        "--hybrid",
                        "ir",
                        "--hybrid-region",
                        "stateful",
                        "--iterations",
                        "1",
                        "--warmups",
                        "0",
                        "--output-dir",
                        str(root),
                        "--json",
                        str(report),
                        str(source),
                    ]
                )
            self.assertEqual(status, 0)
            self.assertEqual(len(compile_program.call_args_list), 2)
            protected = compile_program.call_args_list[0].args[0]
            self.assertEqual(
                protected[protected.index("--hybrid-region") + 1], "stateful"
            )
            self.assertNotIn(
                "--hybrid-region", compile_program.call_args_list[1].args[0]
            )
            self.assertEqual(
                json.loads(report.read_text(encoding="utf-8"))["hybrid_region"],
                "stateful",
            )

    def test_diversity_forwards_region(self):
        with tempfile.TemporaryDirectory(prefix="a2mba-cli-test-") as temporary:
            arguments = diversity.build_parser().parse_args(
                ["--hybrid", "native", "--hybrid-region", "stateful", "sample.c"]
            )

            def compile_object(command, **kwargs):
                Path(command[-1]).write_bytes(b"object")
                return SimpleNamespace(returncode=0, stdout="", stderr="")

            for system_name in ("nt", "posix"):
                with self.subTest(system=system_name):
                    output = Path(temporary) / f"sample-{system_name}.obj"
                    system = SimpleNamespace(
                        name=system_name, fspath=diversity.os.fspath
                    )
                    with patch.object(diversity, "os", system), patch.object(
                        diversity.subprocess, "run", side_effect=compile_object
                    ) as run, patch("builtins.print"):
                        diversity.compile_variant(arguments, [], 17, output)
                    command = run.call_args.args[0]
                    self.assertEqual(
                        command[command.index("--hybrid-region") + 1], "stateful"
                    )
                    self.assertEqual(
                        "-mno-incremental-linker-compatible" in command,
                        system_name == "nt",
                    )

    def test_validate_forwards_region_to_each_helper(self):
        with patch.object(validate, "run") as run, patch("builtins.print"):
            status = validate.main(
                [
                    "--skip-configure",
                    "--skip-build",
                    "--skip-tests",
                    "--hybrid",
                    "ir",
                    "--hybrid-region",
                    "stateful",
                    "--sample",
                    "sample.c",
                ]
            )
        self.assertEqual(status, 0)
        self.assertEqual(run.call_count, 3)
        for call in run.call_args_list:
            command = call.args[0]
            self.assertEqual(command[command.index("--hybrid-region") + 1], "stateful")


if __name__ == "__main__":
    unittest.main()
