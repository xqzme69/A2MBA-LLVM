#!/usr/bin/env python3
"""Compile seeded object variants and check determinism and SHA-256 diversity."""

from __future__ import annotations

import argparse
import hashlib
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class DiversityError(RuntimeError):
    """The diversity experiment could not be completed."""


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


def at_least_two(value: str) -> int:
    number = int(value)
    if number < 2:
        raise argparse.ArgumentTypeError("at least two variants are required")
    return number


def positive_number(value: str) -> float:
    number = float(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
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
    parser.add_argument("--base-seed", type=unsigned_64, default=1)
    parser.add_argument("--variants", type=at_least_two, default=10)
    parser.add_argument("--timeout", type=positive_number, default=120.0)
    parser.add_argument(
        "--output-dir", type=Path, help="keep generated objects in this directory"
    )
    parser.add_argument("source", type=Path)
    parser.add_argument(
        "compiler_args", nargs=argparse.REMAINDER, help="arguments forwarded to Clang"
    )
    return parser


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def compile_variant(
    arguments: argparse.Namespace,
    compiler_args: Sequence[str],
    seed: int,
    output: Path,
) -> str:
    if output.exists():
        raise DiversityError(f"refusing to overwrite {output}")

    command = [
        sys.executable,
        os.fspath(arguments.wrapper.resolve()),
        "--mode",
        arguments.mode,
        "--level",
        arguments.level,
        "--seed",
        str(seed),
        "--functions",
        arguments.functions,
    ]
    if arguments.clang:
        command.extend(("--clang", arguments.clang))
    if arguments.opt:
        command.extend(("--opt", arguments.opt))
    if arguments.plugin:
        command.extend(("--plugin", arguments.plugin))
    command.extend(
        (
            os.fspath(arguments.source.resolve()),
            "-O3",
            *compiler_args,
            "-c",
            "-o",
            os.fspath(output),
        )
    )

    print(f"+ {shlex.join(command)}", flush=True)
    try:
        result = subprocess.run(
            command,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
            timeout=arguments.timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise DiversityError(f"compile failed for seed {seed}: {error}") from error
    if result.returncode != 0:
        if result.stdout:
            print(result.stdout.rstrip(), file=sys.stderr)
        if result.stderr:
            print(result.stderr.rstrip(), file=sys.stderr)
        raise DiversityError(
            f"compile for seed {seed} exited with status {result.returncode}"
        )
    if not output.is_file():
        raise DiversityError(f"Clang reported success without producing {output}")
    try:
        return sha256(output)
    except OSError as error:
        raise DiversityError(f"could not hash {output}: {error}") from error


def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    if not arguments.source.is_file():
        print(
            f"diversity: error: source not found: {arguments.source}", file=sys.stderr
        )
        return 2
    if not arguments.wrapper.is_file():
        print(
            f"diversity: error: wrapper not found: {arguments.wrapper}", file=sys.stderr
        )
        return 2

    compiler_args = list(arguments.compiler_args)
    if compiler_args[:1] == ["--"]:
        compiler_args.pop(0)
    if (
        "-o" in compiler_args
        or "--output" in compiler_args
        or any(argument.startswith("--output=") for argument in compiler_args)
    ):
        print(
            "diversity: error: the script owns Clang's output option", file=sys.stderr
        )
        return 2
    if arguments.base_seed + arguments.variants - 1 > (1 << 64) - 1:
        print("diversity: error: seed range exceeds 2^64-1", file=sys.stderr)
        return 2

    temporary: tempfile.TemporaryDirectory[str] | None = None
    if arguments.output_dir:
        output_dir = arguments.output_dir.resolve()
        try:
            output_dir.mkdir(parents=True, exist_ok=True)
        except OSError as error:
            print(
                f"diversity: error: could not create {output_dir}: {error}",
                file=sys.stderr,
            )
            return 2
    else:
        temporary = tempfile.TemporaryDirectory(prefix="a2mba-diversity-")
        output_dir = Path(temporary.name)

    extension = ".obj" if os.name == "nt" else ".o"
    try:
        first_seed = arguments.base_seed
        first = output_dir / f"seed-{first_seed}-a{extension}"
        repeated = output_dir / f"seed-{first_seed}-b{extension}"
        first_hash = compile_variant(arguments, compiler_args, first_seed, first)
        repeated_hash = compile_variant(arguments, compiler_args, first_seed, repeated)
        if first_hash != repeated_hash:
            raise DiversityError(
                f"same seed {first_seed} produced different object hashes"
            )

        hashes: dict[str, int] = {first_hash: first_seed}
        for offset in range(1, arguments.variants):
            seed = first_seed + offset
            artifact = output_dir / f"seed-{seed}{extension}"
            digest = compile_variant(arguments, compiler_args, seed, artifact)
            if digest in hashes:
                raise DiversityError(
                    f"seeds {hashes[digest]} and {seed} produced the same object hash {digest}"
                )
            hashes[digest] = seed
    except DiversityError as error:
        print(f"diversity: error: {error}", file=sys.stderr)
        return 1
    finally:
        if temporary:
            temporary.cleanup()

    print(f"determinism: pass (seed {arguments.base_seed})")
    print(f"diversity: pass ({len(hashes)}/{arguments.variants} unique SHA-256 hashes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
