#!/usr/bin/env python3
"""Configure, build, test, and smoke-check an A2MBA checkout."""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Sequence


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class ValidationError(RuntimeError):
    """A validation command could not be completed."""


def positive_integer(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("value must be positive")
    return number


def existing_tool(value: str) -> str:
    path = Path(value).expanduser()
    if path.is_file():
        return os.fspath(path.resolve())
    located = shutil.which(value)
    if located:
        return located
    raise argparse.ArgumentTypeError(f"executable not found: {value}")


def show_command(command: Sequence[str]) -> None:
    print(f"+ {shlex.join(command)}", flush=True)


def run(command: Sequence[str], *, cwd: Path | None = None) -> None:
    show_command(command)
    try:
        result = subprocess.run(command, check=False, cwd=cwd)
    except OSError as error:
        raise ValidationError(f"could not start {command[0]}: {error}") from error
    if result.returncode != 0:
        raise ValidationError(f"command exited with status {result.returncode}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--source-dir", type=Path, default=PROJECT_ROOT)
    parser.add_argument("--build-dir", type=Path, default=PROJECT_ROOT / "build")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--llvm-dir", type=Path, help="LLVM 21 CMake package directory")
    parser.add_argument("--cmake", type=existing_tool, default="cmake")
    parser.add_argument("--ctest", type=existing_tool, default="ctest")
    parser.add_argument("--cmake-arg", action="append", default=[], metavar="ARG")
    parser.add_argument("--parallel", type=positive_integer)
    parser.add_argument("--test-timeout", type=positive_integer, default=300)
    parser.add_argument("--clang", help="forwarded to a2mba-clang")
    parser.add_argument("--opt", help="forwarded to a2mba-clang")
    parser.add_argument("--plugin", help="forwarded to a2mba-clang")
    parser.add_argument("--skip-configure", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-tests", action="store_true")
    parser.add_argument("--skip-doctor", action="store_true")
    parser.add_argument(
        "--sample", type=Path, help="also run diversity and benchmark smoke checks"
    )
    parser.add_argument("--diversity-variants", type=positive_integer, default=4)
    parser.add_argument("--benchmark-iterations", type=positive_integer, default=3)
    return parser


def wrapper_overrides(arguments: argparse.Namespace) -> list[str]:
    overrides: list[str] = []
    if arguments.clang:
        overrides.extend(("--clang", arguments.clang))
    if arguments.opt:
        overrides.extend(("--opt", arguments.opt))
    if arguments.plugin:
        overrides.extend(("--plugin", arguments.plugin))
    return overrides


def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    source_dir = arguments.source_dir.resolve()
    build_dir = arguments.build_dir.resolve()
    wrapper = source_dir / "tools" / "a2mba-clang.py"

    try:
        if not arguments.skip_configure:
            configure = [
                arguments.cmake,
                "-S",
                os.fspath(source_dir),
                "-B",
                os.fspath(build_dir),
                f"-DCMAKE_BUILD_TYPE={arguments.config}",
            ]
            if arguments.llvm_dir:
                configure.append(f"-DLLVM_DIR={arguments.llvm_dir.resolve()}")
            configure.extend(arguments.cmake_arg)
            run(configure)

        if not arguments.skip_build:
            build = [
                arguments.cmake,
                "--build",
                os.fspath(build_dir),
                "--config",
                arguments.config,
            ]
            if arguments.parallel:
                build.extend(("--parallel", str(arguments.parallel)))
            run(build)

        if not arguments.skip_tests:
            run(
                [
                    arguments.ctest,
                    "--test-dir",
                    os.fspath(build_dir),
                    "-C",
                    arguments.config,
                    "--output-on-failure",
                    "--timeout",
                    str(arguments.test_timeout),
                ]
            )

        overrides = wrapper_overrides(arguments)
        if not arguments.skip_doctor:
            run([sys.executable, os.fspath(wrapper), "--doctor", *overrides])

        if arguments.sample:
            sample = arguments.sample.resolve()
            run(
                [
                    sys.executable,
                    os.fspath(source_dir / "scripts" / "diversity.py"),
                    "--variants",
                    str(arguments.diversity_variants),
                    *overrides,
                    os.fspath(sample),
                ]
            )
            run(
                [
                    sys.executable,
                    os.fspath(source_dir / "scripts" / "benchmark.py"),
                    "--iterations",
                    str(arguments.benchmark_iterations),
                    *overrides,
                    os.fspath(sample),
                ]
            )
    except ValidationError as error:
        print(f"validate: error: {error}", file=sys.stderr)
        return 1

    print("validation completed successfully")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
