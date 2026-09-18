#!/usr/bin/env python3
"""Run bounded hybrid-rule queries through Z3."""

from __future__ import annotations

import ctypes
import ctypes.util
import importlib.util
from functools import lru_cache
import os
from pathlib import Path
import shutil
import subprocess
import sys

MAX_QUERY_BYTES = 4_000_000
DEFAULT_TIMEOUT_SECONDS = 8.0


def load_z3_library() -> ctypes.CDLL:
    library_candidates = [
        os.environ.get("Z3_LIBRARY"),
        ctypes.util.find_library("z3"),
    ]
    z3_package = importlib.util.find_spec("z3")
    if z3_package and z3_package.origin:
        package_root = Path(z3_package.origin).parent
        library_candidates += [
            str(package_root / directory / filename)
            for directory in ("lib", "bin", "")
            for filename in ("libz3.so", "libz3.dylib", "libz3.dll", "z3.dll")
        ]
    for library_path in filter(None, library_candidates):
        try:
            return ctypes.CDLL(library_path)
        except OSError:
            continue
    raise RuntimeError(
        "Z3 not found: install z3/z3-solver or set Z3_LIBRARY to its shared library"
    )


@lru_cache(maxsize=1)
def library_name() -> str:
    return str(load_z3_library()._name)


def version() -> str:
    executable = shutil.which("z3")
    if executable:
        return subprocess.run(
            [executable, "-version"],
            capture_output=True,
            text=True,
            check=True,
            timeout=5,
        ).stdout.strip()
    library = load_z3_library()
    library.Z3_get_full_version.restype = ctypes.c_char_p
    return library.Z3_get_full_version().decode("utf-8")


def check(query: str, seconds: float = DEFAULT_TIMEOUT_SECONDS) -> str:
    if len(query.encode("utf-8")) > MAX_QUERY_BYTES:
        raise ValueError("SMT query is too large")
    executable = shutil.which("z3")
    command = (
        [executable, "-in", "-smt2"]
        if executable
        else [sys.executable, "-S", __file__, "--worker"]
    )
    environment = os.environ.copy()
    if not executable:
        environment["Z3_LIBRARY"] = library_name()
    try:
        completed = subprocess.run(
            command,
            input=query,
            capture_output=True,
            text=True,
            timeout=seconds,
            check=False,
            env=environment,
        )
    except subprocess.TimeoutExpired:
        return "unknown: wall-timeout"
    if completed.returncode:
        error_text = (completed.stderr or completed.stdout)[:1000]
        raise RuntimeError(f"Z3 exited {completed.returncode}: {error_text}")
    answer = completed.stdout.strip()
    if answer not in ("sat", "unsat", "unknown"):
        raise RuntimeError(f"Unexpected Z3 output: {answer[:1000]}")
    return answer


def run_library_worker() -> None:
    library = load_z3_library()
    opaque_pointer = ctypes.c_void_p
    library.Z3_mk_config.restype = opaque_pointer
    library.Z3_mk_context.argtypes = [opaque_pointer]
    library.Z3_mk_context.restype = opaque_pointer
    library.Z3_del_config.argtypes = [opaque_pointer]
    library.Z3_del_context.argtypes = [opaque_pointer]
    library.Z3_eval_smtlib2_string.argtypes = [opaque_pointer, ctypes.c_char_p]
    library.Z3_eval_smtlib2_string.restype = ctypes.c_char_p
    configuration = library.Z3_mk_config()
    context = library.Z3_mk_context(configuration)
    library.Z3_del_config(configuration)
    try:
        query = sys.stdin.buffer.read(MAX_QUERY_BYTES + 1)
        if len(query) > MAX_QUERY_BYTES:
            raise ValueError("query too large")
        result = library.Z3_eval_smtlib2_string(context, query)
        if result is None:
            raise RuntimeError("Z3 returned null")
        sys.stdout.write(result.decode("utf-8"))
    finally:
        library.Z3_del_context(context)


if __name__ == "__main__":
    try:
        if sys.argv[1:] == ["--worker"]:
            run_library_worker()
        else:
            print(version())
    except Exception as error:
        print(f"SMT backend: {error}", file=sys.stderr)
        raise SystemExit(2)
