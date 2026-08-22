#!/usr/bin/env python3
"""Measure whole-process runtime and file-size overhead against plain Clang."""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Sequence


PROJECT_ROOT = Path(__file__).resolve().parents[1]
LLVM_MAJOR = 21


class BenchmarkError(RuntimeError):
    """The benchmark could not be completed."""


def positive_integer(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("value must be positive")
    return number


def nonnegative_integer(value: str) -> int:
    number = int(value)
    if number < 0:
        raise argparse.ArgumentTypeError("value may not be negative")
    return number


def positive_number(value: str) -> float:
    number = float(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return number


def unsigned_64(value: str) -> int:
    try:
        number = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "seed must be an unsigned 64-bit integer"
        ) from error
    if not 0 <= number <= (1 << 64) - 1:
        raise argparse.ArgumentTypeError("seed must be in the range 0..2^64-1")
    return number


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument(
        "--wrapper", type=Path, default=PROJECT_ROOT / "tools" / "a2mba-clang.py"
    )
    parser.add_argument("--clang")
    parser.add_argument("--opt")
    parser.add_argument("--plugin")
    parser.add_argument("--mode", choices=("verified", "paper"), default="verified")
    parser.add_argument(
        "--level",
        choices=("light", "balanced", "medium", "heavy"),
        default="balanced",
    )
    parser.add_argument("--functions", default="all")
    parser.add_argument("--seed", type=unsigned_64, default=1)
    parser.add_argument("--iterations", type=positive_integer, default=10)
    parser.add_argument("--warmups", type=nonnegative_integer, default=2)
    parser.add_argument("--timeout", type=positive_number, default=30.0)
    parser.add_argument("--run-arg", action="append", default=[], metavar="ARG")
    parser.add_argument("--run-cwd", type=Path)
    parser.add_argument(
        "--output-dir", type=Path, help="keep the two compiled executables"
    )
    parser.add_argument(
        "--json", type=Path, help="write measured results to a new JSON file"
    )
    parser.add_argument("source", type=Path)
    parser.add_argument(
        "compiler_args", nargs=argparse.REMAINDER, help="arguments forwarded to Clang"
    )
    return parser


def resolve_executable(value: str) -> Path | None:
    candidate = Path(value).expanduser()
    if candidate.is_file():
        return candidate.resolve()
    located = shutil.which(value)
    return Path(located).resolve() if located else None


def find_clang(requested: str | None) -> Path:
    if requested:
        clang = resolve_executable(requested)
        if not clang:
            raise BenchmarkError(f"Clang not found: {requested}")
    else:
        clang = None
        for name in ("clang-21", "clang++-21", "clang", "clang++"):
            clang = resolve_executable(name)
            if clang:
                break
        if not clang:
            raise BenchmarkError("Clang not found; pass --clang")

    try:
        result = subprocess.run(
            [os.fspath(clang), "--version"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise BenchmarkError(f"could not query Clang version: {error}") from error
    match = re.search(r"\bclang version\s+(\d+)", result.stdout, re.IGNORECASE)
    if not match:
        raise BenchmarkError("could not parse Clang version")
    if int(match.group(1)) != LLVM_MAJOR:
        raise BenchmarkError(
            f"unsupported Clang/LLVM major {match.group(1)}; expected {LLVM_MAJOR}"
        )
    return clang


def compile_program(command: Sequence[str], output: Path) -> None:
    if output.exists():
        raise BenchmarkError(f"refusing to overwrite {output}")
    print(f"+ {shlex.join(command)}", flush=True)
    try:
        result = subprocess.run(
            command,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
        )
    except OSError as error:
        raise BenchmarkError(f"could not start compiler: {error}") from error
    if result.returncode != 0:
        if result.stdout:
            print(result.stdout.rstrip(), file=sys.stderr)
        if result.stderr:
            print(result.stderr.rstrip(), file=sys.stderr)
        raise BenchmarkError(f"compiler exited with status {result.returncode}")
    if not output.is_file():
        raise BenchmarkError(f"compiler reported success without producing {output}")


def run_program(
    executable: Path, run_args: Sequence[str], cwd: Path, timeout: float
) -> tuple[float, bytes, bytes]:
    started = time.perf_counter()
    try:
        result = subprocess.run(
            [os.fspath(executable), *run_args],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=cwd,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise BenchmarkError(f"could not run {executable.name}: {error}") from error
    elapsed = time.perf_counter() - started
    if result.returncode != 0:
        raise BenchmarkError(
            f"{executable.name} exited with status {result.returncode}"
        )
    return elapsed, result.stdout, result.stderr


def run_pair(
    baseline: Path,
    protected: Path,
    run_args: Sequence[str],
    cwd: Path,
    timeout: float,
    protected_first: bool,
) -> tuple[float, float]:
    order = (protected, baseline) if protected_first else (baseline, protected)
    measurements: dict[Path, tuple[float, bytes, bytes]] = {}
    for executable in order:
        measurements[executable] = run_program(executable, run_args, cwd, timeout)
    if measurements[baseline][1:] != measurements[protected][1:]:
        raise BenchmarkError("baseline and protected program output differ")
    return measurements[baseline][0], measurements[protected][0]


def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    if not arguments.source.is_file():
        print(
            f"benchmark: error: source not found: {arguments.source}", file=sys.stderr
        )
        return 2
    if not arguments.wrapper.is_file():
        print(
            f"benchmark: error: wrapper not found: {arguments.wrapper}", file=sys.stderr
        )
        return 2

    compiler_args = list(arguments.compiler_args)
    if compiler_args[:1] == ["--"]:
        compiler_args.pop(0)
    forbidden = {"-o", "--output", "-c", "-S", "-E", "-emit-llvm", "-fsyntax-only"}
    if forbidden.intersection(compiler_args) or any(
        argument.startswith("--output=") for argument in compiler_args
    ):
        print(
            "benchmark: error: compiler arguments may not change the output mode or path",
            file=sys.stderr,
        )
        return 2

    temporary: tempfile.TemporaryDirectory[str] | None = None
    if arguments.output_dir:
        output_dir = arguments.output_dir.resolve()
        try:
            output_dir.mkdir(parents=True, exist_ok=True)
        except OSError as error:
            print(
                f"benchmark: error: could not create {output_dir}: {error}",
                file=sys.stderr,
            )
            return 2
    else:
        temporary = tempfile.TemporaryDirectory(prefix="a2mba-benchmark-")
        output_dir = Path(temporary.name)

    suffix = ".exe" if os.name == "nt" else ""
    baseline = output_dir / f"baseline{suffix}"
    protected = output_dir / f"protected{suffix}"
    source = arguments.source.resolve()

    try:
        clang = find_clang(arguments.clang)
        protected_command = [
            sys.executable,
            os.fspath(arguments.wrapper.resolve()),
            "--clang",
            os.fspath(clang),
            "--mode",
            arguments.mode,
            "--level",
            arguments.level,
            "--seed",
            str(arguments.seed),
            "--functions",
            arguments.functions,
        ]
        if arguments.plugin:
            protected_command.extend(("--plugin", arguments.plugin))
        if arguments.opt:
            protected_command.extend(("--opt", arguments.opt))
        protected_command.extend(
            (os.fspath(source), "-O3", *compiler_args, "-o", os.fspath(protected))
        )
        baseline_command = [
            os.fspath(clang),
            os.fspath(source),
            "-O3",
            *compiler_args,
            "-o",
            os.fspath(baseline),
        ]

        compile_program(protected_command, protected)
        compile_program(baseline_command, baseline)

        run_cwd = (arguments.run_cwd or source.parent).resolve()
        for index in range(arguments.warmups):
            run_pair(
                baseline,
                protected,
                arguments.run_arg,
                run_cwd,
                arguments.timeout,
                protected_first=bool(index % 2),
            )

        baseline_times: list[float] = []
        protected_times: list[float] = []
        for index in range(arguments.iterations):
            baseline_time, protected_time = run_pair(
                baseline,
                protected,
                arguments.run_arg,
                run_cwd,
                arguments.timeout,
                protected_first=bool(index % 2),
            )
            baseline_times.append(baseline_time)
            protected_times.append(protected_time)

        baseline_median = statistics.median(baseline_times)
        protected_median = statistics.median(protected_times)
        baseline_size = baseline.stat().st_size
        protected_size = protected.stat().st_size
        results = {
            "source": os.fspath(source),
            "mode": arguments.mode,
            "level": arguments.level,
            "seed": arguments.seed,
            "iterations": arguments.iterations,
            "warmups": arguments.warmups,
            "runtime_seconds": {
                "baseline_median": baseline_median,
                "protected_median": protected_median,
                "ratio": protected_median / baseline_median,
            },
            "file_size_bytes": {
                "baseline": baseline_size,
                "protected": protected_size,
                "ratio": protected_size / baseline_size,
            },
        }

        if arguments.json:
            result_path = arguments.json.resolve()
            try:
                result_path.parent.mkdir(parents=True, exist_ok=True)
                with result_path.open("x", encoding="utf-8", newline="\n") as stream:
                    json.dump(results, stream, indent=2)
                    stream.write("\n")
            except FileExistsError as error:
                raise BenchmarkError(f"refusing to overwrite {result_path}") from error
            except OSError as error:
                raise BenchmarkError(
                    f"could not write {result_path}: {error}"
                ) from error
    except BenchmarkError as error:
        print(f"benchmark: error: {error}", file=sys.stderr)
        return 1
    finally:
        if temporary:
            temporary.cleanup()

    print(
        f"runtime median: {baseline_median:.6f}s -> {protected_median:.6f}s ({results['runtime_seconds']['ratio']:.3f}x)"
    )
    print(
        f"file size: {baseline_size} -> {protected_size} bytes ({results['file_size_bytes']['ratio']:.3f}x)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
