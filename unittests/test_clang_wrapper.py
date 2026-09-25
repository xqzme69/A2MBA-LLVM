import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "a2mba_clang", Path(__file__).resolve().parents[1] / "tools" / "a2mba-clang.py"
)
wrapper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wrapper)


class ClangWrapperTests(unittest.TestCase):
    def test_wrapper_options_do_not_consume_clang_option_values(self):
        parser = wrapper.build_parser()
        for option in wrapper.OPTION_VALUES:
            for value in ("--seed", "--seed=7", "--stats", "--help", "--doctor", "-h"):
                with self.subTest(option=option, value=value):
                    arguments, clang = wrapper.parse_arguments(
                        parser,
                        [
                            "--seed=913",
                            "-O3",
                            option,
                            value,
                            "--hybrid",
                            "ir",
                            "-c",
                            "source.c",
                        ],
                    )
                    self.assertEqual(arguments.seed, 913)
                    self.assertEqual(arguments.hybrid, "ir")
                    self.assertFalse(arguments.stats)
                    self.assertFalse(arguments.doctor)
                    self.assertEqual(clang, ["-O3", option, value, "-c", "source.c"])

    def test_separator_keeps_wrapper_like_names_as_clang_inputs(self):
        arguments, clang = wrapper.parse_arguments(
            wrapper.build_parser(), ["--stats", "-O3", "--", "--seed", "--doctor"]
        )
        self.assertTrue(arguments.stats)
        self.assertIsNone(arguments.seed)
        self.assertFalse(arguments.doctor)
        self.assertEqual(clang, ["-O3", "--", "--seed", "--doctor"])

    def test_missing_wrapper_option_value_is_rejected(self):
        parser = wrapper.build_parser()
        with patch.object(parser, "error", side_effect=ValueError) as error:
            with self.assertRaises(ValueError):
                wrapper.parse_arguments(parser, ["-I", "--seed", "--seed"])
            error.assert_called_once()
            self.assertIn("expected one argument", error.call_args.args[0])

    def test_compiler_lookup_preserves_driver_alias(self):
        alias = Path.cwd() / "clang++"
        resolved = alias.with_name("clang")
        with patch.object(Path, "is_file", return_value=True), patch.object(
            Path, "resolve", return_value=resolved
        ):
            self.assertEqual(wrapper.resolve_executable(str(alias)), alias)
        with patch.object(Path, "is_file", return_value=False), patch.object(
            wrapper.shutil, "which", return_value=str(alias)
        ), patch.object(Path, "resolve", return_value=resolved):
            self.assertEqual(wrapper.resolve_executable("clang++"), alias)

    def test_source_indices_do_not_consume_option_values(self):
        arguments = ["-include", "header.c", "-I", "directory.c", "source.c", "-O3"]
        self.assertEqual(wrapper.source_argument_indices(arguments), [4])

    def test_standard_input_is_a_source(self):
        self.assertEqual(wrapper.source_argument_indices(["-x", "c", "-"]), [2])

    def test_joined_language_selects_extensionless_sources(self):
        with patch.object(Path, "is_file", return_value=True):
            self.assertEqual(
                wrapper.source_argument_indices(
                    ["-xc", "source", "-xnone", "object.o"]
                ),
                [1],
            )

    def test_working_directory_uses_last_driver_option(self):
        self.assertEqual(
            wrapper.clang_working_directory(
                [
                    "-working-directory",
                    "first",
                    "-working-directory=last",
                    "-I",
                    "-working-directory=include",
                    "-Xclang",
                    "-working-directory=frontend",
                    "--",
                    "-working-directory=filename",
                ]
            ),
            Path.cwd() / "last",
        )

    def test_extensionless_source_uses_clang_working_directory(self):
        with tempfile.TemporaryDirectory(prefix="a2mba-working-") as temporary:
            directory = Path(temporary)
            (directory / "extensionless").write_text("int value;\n", encoding="utf-8")
            self.assertEqual(
                wrapper.source_argument_indices(
                    ["-working-directory", temporary, "-xc", "extensionless"]
                ),
                [3],
            )

    def test_last_optimization_option_wins(self):
        self.assertFalse(wrapper.optimization_enabled(["-O3", "-O0"]))
        self.assertTrue(wrapper.optimization_enabled(["-O0", "-O3"]))

    def test_option_values_and_trailing_paths_do_not_select_actions(self):
        self.assertTrue(wrapper.optimization_enabled(["-O3", "-I", "-O0"]))
        self.assertFalse(wrapper.optimization_enabled(["-O0", "--", "-O3"]))
        self.assertTrue(wrapper.machine_codegen_requested(["-I", "-E", "source.c"]))
        self.assertTrue(wrapper.machine_codegen_requested(["source.c", "--", "-E"]))

    def test_last_output_option_wins(self):
        self.assertEqual(
            wrapper.clang_output(["-ofirst", "--output=second", "-o", "last"]), "last"
        )
        self.assertIsNone(
            wrapper.clang_output(["-object-file-name=source.o", "source.c"])
        )

    def test_output_ignores_other_option_values(self):
        self.assertEqual(
            wrapper.clang_output(["-o", "app.o", "-Xlinker", "-old_library"]), "app.o"
        )
        self.assertIsNone(wrapper.clang_output(["source.c", "--", "-output.c"]))

    def test_stages_keep_forwarded_arguments_intact(self):
        arguments = ["-Xclang", "-S", "-c", "source.c", "-o", "source.o"]
        self.assertEqual(
            wrapper.strip_output_and_action(arguments, {3}, 3),
            ["-Xclang", "-S", "source.c"],
        )
        arguments = ["-Xclang", "-MD", "-c", "source.c"]
        final = wrapper.final_codegen_arguments(arguments, {3: Path("protected.bc")})
        self.assertEqual(final[-4:], ["-Xclang", "-MD", "-c", "protected.bc"])

    def test_missing_option_values_are_rejected(self):
        for option in ("-o", "-I", "-MF", "-x", "-Xclang"):
            with self.subTest(option=option), self.assertRaisesRegex(
                wrapper.WrapperError, "missing argument"
            ):
                list(wrapper.clang_argument_groups(["source.c", option]))

    def test_separator_preserves_flag_like_filenames(self):
        arguments = ["-O3", "--", "-output.c"]
        self.assertEqual(wrapper.source_argument_indices(arguments), [2])
        self.assertEqual(
            wrapper.strip_output_and_action(arguments, {2}, 2),
            ["-O3", "./-output.c"],
        )

    def test_dependency_defaults_use_public_output_names(self):
        for action in ("-c", "-S"):
            with self.subTest(action=action):
                self.assertEqual(
                    wrapper.dependency_arguments(["-MMD", action, "source/mix.c"], 2),
                    ["-MF", "mix.d", "-MQ", "mix.o"],
                )
        self.assertEqual(
            wrapper.dependency_arguments(["-MD", "mix.c", "-o", "app.exe"], 1),
            ["-MF", "app.d", "-MQ", "app.exe"],
        )

    def test_explicit_dependency_options_are_preserved(self):
        self.assertEqual(
            wrapper.dependency_arguments(
                ["-MD", "-MFcustom.d", "-MT", "target", "source.c"], 4
            ),
            [],
        )

    def test_staged_output_defaults_match_clang(self):
        for actions, expected in (
            (["-S", "-emit-llvm"], "mix.ll"),
            (["-c", "-emit-llvm"], "mix.bc"),
            (["-S"], "mix.s"),
            (["-c"], "mix.o"),
        ):
            with self.subTest(actions=actions), patch.object(
                wrapper.subprocess, "run", return_value=SimpleNamespace(returncode=0)
            ) as run:
                arguments = ["-O3", "nested/mix.c", *actions]
                self.assertEqual(
                    wrapper.run_staged_compile(
                        Path("clang"), Path("a2mba-opt"), None, arguments, [1], {}
                    ),
                    0,
                )
                self.assertEqual(run.call_args.args[0][-2:], ["-o", expected])

    def test_llvm_outputs_use_clang_working_directory(self):
        directory = Path.cwd() / "nested"
        absolute_output = Path.cwd() / "elsewhere.ll"
        for output, expected in (
            ([], str(directory / "mix.ll")),
            (["-o", "named.ll"], str(directory / "named.ll")),
            (["-o", str(absolute_output)], str(absolute_output)),
            (["-o", "-"], "-"),
        ):
            with self.subTest(output=output), patch.object(
                wrapper.subprocess, "run", return_value=SimpleNamespace(returncode=0)
            ) as run:
                arguments = [
                    "-O3",
                    f"-working-directory={directory}",
                    "mix.c",
                    "-S",
                    "-emit-llvm",
                    *output,
                ]
                self.assertEqual(
                    wrapper.run_staged_compile(
                        Path("clang"), Path("a2mba-opt"), None, arguments, [2], {}
                    ),
                    0,
                )
                self.assertEqual(run.call_args.args[0][-2:], ["-o", expected])

    def test_multiple_sources_are_compiled_separately_then_linked(self):
        with patch.object(
            wrapper.subprocess, "run", return_value=SimpleNamespace(returncode=0)
        ) as run:
            arguments = ["-O3", "first.c", "second.c", "-lmath", "-o", "app.exe"]
            self.assertEqual(
                wrapper.run_staged_compile(
                    Path("clang"), Path("a2mba-opt"), None, arguments, [1, 2], {}
                ),
                0,
            )
            commands = [call.args[0] for call in run.call_args_list]
            self.assertEqual(len(commands), 5)
            self.assertIn("first.c", commands[0])
            self.assertNotIn("second.c", commands[0])
            self.assertIn("second.c", commands[2])
            self.assertNotIn("first.c", commands[2])
            self.assertEqual(
                [Path(arg).name for arg in commands[-1] if arg.endswith(".bc")],
                ["protected-0.bc", "protected-1.bc"],
            )
            self.assertEqual(commands[-1][-3:], ["-lmath", "-o", "app.exe"])

    def test_staging_stops_after_each_child_failure(self):
        for stage in range(5):
            with self.subTest(stage=stage), patch.object(
                wrapper.subprocess,
                "run",
                side_effect=[SimpleNamespace(returncode=0)] * stage
                + [SimpleNamespace(returncode=7)],
            ) as run:
                self.assertEqual(
                    wrapper.run_staged_compile(
                        Path("clang"),
                        Path("a2mba-opt"),
                        None,
                        ["first.c", "second.c", "-O3"],
                        [0, 1],
                        {},
                    ),
                    7,
                )
                self.assertEqual(run.call_count, stage + 1)

    def test_staging_reports_each_process_launch_failure(self):
        for stage in range(5):
            with self.subTest(stage=stage), patch.object(
                wrapper.subprocess,
                "run",
                side_effect=[SimpleNamespace(returncode=0)] * stage
                + [FileNotFoundError("missing executable")],
            ) as run, patch("builtins.print") as diagnostic:
                self.assertEqual(
                    wrapper.run_staged_compile(
                        Path("clang"),
                        Path("a2mba-opt"),
                        None,
                        ["first.c", "second.c", "-O3"],
                        [0, 1],
                        {},
                    ),
                    2,
                )
                self.assertEqual(run.call_count, stage + 1)
                self.assertIn("could not execute", diagnostic.call_args.args[0])

    def test_response_files_are_expanded_before_inspecting_flags(self):
        expanded = ["-O3", "-c", "source.c", "-o", "output.o"]
        with patch.object(
            wrapper.subprocess,
            "run",
            return_value=SimpleNamespace(returncode=0, stdout=json.dumps(expanded)),
        ) as run:
            self.assertEqual(
                wrapper.expand_response_files(
                    Path("clang"), Path("a2mba-opt"), ["@options.rsp"], {}
                ),
                expanded,
            )
            self.assertEqual(
                run.call_args.args[0][1:3], ["--a2mba-expand-args", "posix"]
            )

    def test_missing_or_invalid_response_expansion_is_rejected(self):
        for output in ("not json", "{}", "[1]", '["@missing.rsp"]'):
            with self.subTest(output=output), patch.object(
                wrapper.subprocess,
                "run",
                return_value=SimpleNamespace(returncode=0, stdout=output),
            ), self.assertRaises(wrapper.WrapperError):
                wrapper.expand_response_files(
                    Path("clang"), Path("a2mba-opt"), ["@options.rsp"], {}
                )

    def test_response_expansion_propagates_failures(self):
        with patch.object(
            wrapper.subprocess,
            "run",
            return_value=SimpleNamespace(returncode=2, stderr="recursive expansion"),
        ), self.assertRaisesRegex(wrapper.WrapperError, "recursive expansion"):
            wrapper.expand_response_files(
                Path("clang"), Path("a2mba-opt"), ["@options.rsp"], {}
            )


if __name__ == "__main__":
    unittest.main()
