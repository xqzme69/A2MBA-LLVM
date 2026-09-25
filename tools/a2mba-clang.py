#!/usr/bin/env python3
"""Load the A2MBA LLVM 21 pass into Clang without hiding Clang's CLI."""

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterator, NoReturn, Sequence

LLVM_MAJOR = 21
PLUGIN_ENV = "A2MBA_PLUGIN"
CLANG_ENV = "A2MBA_CLANG"
OPT_ENV = "A2MBA_OPT"
SOURCE_SUFFIXES = frozenset(
    {".bc", ".c", ".c++", ".cc", ".cpp", ".cxx", ".i", ".ii", ".ll", ".m", ".mm"}
)
OPTION_VALUES = frozenset(
    {
        "-B",
        "-D",
        "-I",
        "-L",
        "-MF",
        "-MJ",
        "-MQ",
        "-MT",
        "-U",
        "-Xassembler",
        "-Xclang",
        "-Xlinker",
        "-Xpreprocessor",
        "--gcc-toolchain",
        "--output",
        "--sysroot",
        "--target",
        "-arch",
        "-gcc-toolchain",
        "-idirafter",
        "-iframework",
        "-imacros",
        "-include",
        "-iquote",
        "-isystem",
        "-isysroot",
        "-mllvm",
        "-o",
        "-resource-dir",
        "-serialize-diagnostics",
        "-target",
        "-working-directory",
        "-x",
    }
)


class WrapperError(RuntimeError):
    """A user-facing wrapper configuration error."""


def fail(message: str) -> NoReturn:
    raise WrapperError(message)


def parse_seed(value: str) -> int:
    try:
        seed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "seed must be an unsigned 64-bit integer"
        ) from error
    if not 0 <= seed <= (1 << 64) - 1:
        raise argparse.ArgumentTypeError("seed must be in the range 0..2^64-1")
    return seed


def parse_functions(value: str) -> str:
    if any(separator in value for separator in (";", "\r", "\n")):
        raise argparse.ArgumentTypeError("functions may not contain config separators")
    if value in {"annotated", "all"}:
        return value
    if value.startswith("regex:") and value != "regex:":
        return value
    raise argparse.ArgumentTypeError(
        "functions must be annotated, all, or regex:<pattern>"
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="a2mba-clang",
        allow_abbrev=False,
        description="Run LLVM 21 Clang with the A2MBA pass plugin.",
        epilog="Unrecognized arguments are treated as Clang arguments.",
    )
    parser.add_argument(
        "--doctor",
        action="store_true",
        help="check Clang and the A2MBA pass entry point",
    )
    parser.add_argument(
        "--plugin", metavar="PATH", help=f"plugin path (or ${PLUGIN_ENV}) on Linux"
    )
    parser.add_argument("--clang", metavar="PATH", help=f"Clang path (or ${CLANG_ENV})")
    parser.add_argument("--opt", metavar="PATH", help=f"opt path (or ${OPT_ENV})")
    parser.add_argument("--mode", choices=("verified", "paper"), default="verified")
    parser.add_argument(
        "--level",
        choices=("light", "balanced", "medium", "heavy"),
        default="balanced",
    )
    parser.add_argument(
        "--hybrid",
        choices=("off", "ir", "native"),
        default="off",
        help="select the bounded hybrid MBA emitter",
    )
    parser.add_argument(
        "--hybrid-layers",
        choices=("none", "profile", "context-adc", "context-sbb", "context-random"),
        default="none",
        help="compose ContextTrap and ADC/SBB around a hybrid expression",
    )
    parser.add_argument(
        "--hybrid-region",
        choices=("none", "stateful"),
        default="none",
        help="encode eligible straight-line hybrid regions with carried state",
    )
    parser.add_argument(
        "--seed", type=parse_seed, help="deterministic unsigned 64-bit seed"
    )
    parser.add_argument("--functions", type=parse_functions, default="annotated")
    parser.add_argument(
        "--stats", action="store_true", help="print plugin transformation statistics"
    )
    return parser


def parse_arguments(
    parser: argparse.ArgumentParser, arguments: Sequence[str]
) -> tuple[argparse.Namespace, list[str]]:
    wrapper_arguments: list[str] = []
    clang_arguments: list[str] = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == "--":
            clang_arguments.extend(arguments[index:])
            break

        destination = clang_arguments
        count = 1
        if argument in OPTION_VALUES:
            count = 2
        else:
            action = parser._option_string_actions.get(argument.partition("=")[0])
            if action is not None:
                destination = wrapper_arguments
                if action.nargs != 0 and "=" not in argument:
                    count = 2
        destination.extend(arguments[index : index + count])
        index += count
    return parser.parse_args(wrapper_arguments), clang_arguments


def resolve_executable(value: str) -> Path | None:
    candidate = Path(value).expanduser()
    if candidate.is_file():
        return candidate.absolute()
    located = shutil.which(value)
    return Path(located).absolute() if located else None


def find_clang(explicit: str | None, environment: dict[str, str]) -> Path:
    requested = explicit or environment.get(CLANG_ENV)
    if requested:
        clang = resolve_executable(requested)
        if clang:
            return clang
        fail(f"Clang not found: {requested}")

    for name in ("clang-21", "clang++-21", "clang", "clang++"):
        clang = resolve_executable(name)
        if clang:
            return clang
    fail(f"Clang not found; pass --clang or set {CLANG_ENV}")


def clang_version(clang: Path) -> tuple[int, int, int, str]:
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
        fail(f"could not query Clang version: {error}")

    output = result.stdout.strip()
    match = re.search(
        r"\bclang version\s+(\d+)(?:\.(\d+))?(?:\.(\d+))?", output, re.IGNORECASE
    )
    if not match:
        fail(f"could not parse Clang version from {clang}")
    version = tuple(int(part or 0) for part in match.groups())
    if version[0] != LLVM_MAJOR:
        fail(
            f"unsupported Clang/LLVM major {version[0]}; A2MBA requires LLVM {LLVM_MAJOR}.x"
        )
    return version[0], version[1], version[2], output


def find_opt(
    explicit: str | None,
    environment: dict[str, str],
    clang: Path,
    clang_release: tuple[int, int, int],
) -> Path:
    requested = explicit or environment.get(OPT_ENV)
    script_directory = Path(__file__).resolve().parent
    project = script_directory.parent
    candidates = (
        [requested]
        if requested
        else [
            os.fspath(
                script_directory / ("a2mba-opt.exe" if os.name == "nt" else "a2mba-opt")
            ),
            os.fspath(project / "build" / "bin" / "Release" / "a2mba-opt.exe"),
            os.fspath(project / "build" / "bin" / "a2mba-opt.exe"),
            os.fspath(clang.with_name("opt.exe" if os.name == "nt" else "opt")),
            "opt-21",
            "opt",
        ]
    )
    opt = None
    for candidate in candidates:
        if candidate:
            opt = resolve_executable(candidate)
        if opt:
            break
    if not opt:
        fail(f"opt not found; pass --opt or set {OPT_ENV}")

    try:
        result = subprocess.run(
            [os.fspath(opt), "--version"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        fail(f"could not query opt version: {error}")
    match = re.search(
        r"\bLLVM version\s+(\d+)(?:\.(\d+))?(?:\.(\d+))?",
        result.stdout,
        re.IGNORECASE,
    )
    if not match:
        fail(f"could not parse LLVM version from {opt}")
    opt_release = tuple(int(part or 0) for part in match.groups())
    if opt_release != clang_release:
        fail(
            "opt version "
            f"{'.'.join(map(str, opt_release))} does not match Clang "
            f"{'.'.join(map(str, clang_release))}"
        )
    return opt


def plugin_names(version: tuple[int, int, int]) -> tuple[str, ...]:
    suffix = ".dll" if os.name == "nt" else ".so"
    full_version = ".".join(str(part) for part in version)
    return (
        f"a2mba-llvm{full_version}{suffix}",
        f"a2mba-llvm{version[0]}{suffix}",
        f"A2MBA{suffix}",
        f"libA2MBA{suffix}",
    )


def plugin_directories() -> tuple[Path, ...]:
    script_directory = Path(__file__).resolve().parent
    project = script_directory.parent
    install_prefix = project.parent
    return (
        script_directory,
        project / "build" / "bin" / "Release",
        project / "build" / "bin",
        project / "build" / "lib" / "a2mba",
        project / "build" / "lib" / "Release",
        project / "build" / "Release",
        project / "build" / "lib",
        project / "build",
        project / "lib" / "a2mba",
        project / "lib",
        project,
        install_prefix / "lib" / "a2mba",
        install_prefix / "lib64" / "a2mba",
    )


def validate_plugin_tag(plugin: Path, version: tuple[int, int, int]) -> None:
    match = re.search(
        r"llvm(\d+)(?:[._-](\d+))?(?:[._-](\d+))?", plugin.name, re.IGNORECASE
    )
    if not match:
        return
    tagged = tuple(int(part) if part is not None else None for part in match.groups())
    for actual, expected in zip(version, tagged):
        if expected is not None and actual != expected:
            fail(
                f"plugin filename targets LLVM {'.'.join(str(part) for part in tagged if part is not None)}, "
                f"but Clang is {'.'.join(str(part) for part in version)}"
            )


def find_plugin(
    explicit: str | None,
    environment: dict[str, str],
    version: tuple[int, int, int],
) -> Path:
    requested = explicit or environment.get(PLUGIN_ENV)
    if requested:
        plugin = Path(requested).expanduser()
        if not plugin.is_file():
            fail(f"A2MBA plugin not found: {requested}")
        plugin = plugin.resolve()
        validate_plugin_tag(plugin, version)
        return plugin

    names = plugin_names(version)
    suffix = ".dll" if os.name == "nt" else ".so"
    full_version = ".".join(str(part) for part in version)
    for directory in plugin_directories():
        tagged_matches = sorted(
            path
            for path in directory.glob(f"a2mba-llvm{full_version}*{suffix}")
            if path.is_file()
        )
        if tagged_matches:
            plugin = tagged_matches[0].resolve()
            validate_plugin_tag(plugin, version)
            return plugin
        for name in names:
            plugin = directory / name
            if plugin.is_file():
                plugin = plugin.resolve()
                validate_plugin_tag(plugin, version)
                return plugin

    for directory in plugin_directories():
        if not directory.is_dir():
            continue
        for pattern in (f"A2MBA*{suffix}", f"a2mba*{suffix}", f"libA2MBA*{suffix}"):
            matches = sorted(path for path in directory.glob(pattern) if path.is_file())
            for match in matches:
                plugin = match.resolve()
                try:
                    validate_plugin_tag(plugin, version)
                except WrapperError:
                    continue
                return plugin

    fail(f"A2MBA plugin not found; pass --plugin or set {PLUGIN_ENV}")


def make_options(arguments: argparse.Namespace) -> str:
    options = [
        f"mode={arguments.mode}",
        f"level={arguments.level}",
        f"hybrid={arguments.hybrid}",
        f"hybrid-layers={arguments.hybrid_layers}",
        f"hybrid-region={arguments.hybrid_region}",
        f"functions={arguments.functions}",
        f"stats={'true' if arguments.stats else 'false'}",
    ]
    if arguments.seed is not None:
        options.append(f"seed={arguments.seed}")
    return ";".join(options)


def clang_target(clang: Path) -> str:
    try:
        result = subprocess.run(
            [os.fspath(clang), "-dumpmachine"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            errors="replace",
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired):
        return "unknown"
    return (
        result.stdout.strip()
        if result.returncode == 0 and result.stdout.strip()
        else "unknown"
    )


def is_supported_target(target: str) -> bool:
    normalized = target.lower()
    return normalized.startswith("x86_64-") and (
        "linux" in normalized or "windows" in normalized
    )


def clang_argument_groups(
    arguments: Sequence[str],
) -> Iterator[tuple[int, str | None, list[str]]]:
    index = 0
    options = True
    while index < len(arguments):
        argument = arguments[index]
        if options and argument == "--":
            options = False
            index += 1
            continue
        option = None
        if (
            options
            and argument != "-"
            and (argument.startswith("-") or argument in {"/Od", "/O1", "/O2", "/Ox"})
        ):
            option = argument
        count = 2 if option in OPTION_VALUES else 1
        if index + count > len(arguments):
            fail(f"missing argument to {option}")
        tokens = list(arguments[index : index + count])
        if not options and argument.startswith("-") and argument != "-":
            tokens[0] = f"./{argument}"
        yield index, option, tokens
        index += count


def optimization_enabled(arguments: Sequence[str]) -> bool:
    enabled = False
    for _, argument, _ in clang_argument_groups(arguments):
        if argument is None:
            continue
        if argument in {"-O0", "/Od"}:
            enabled = False
        elif re.fullmatch(r"-O(?:[1-4]|g|s|z|fast)?", argument) or argument in {
            "/O1",
            "/O2",
            "/Ox",
        }:
            enabled = True
    return enabled


def machine_codegen_requested(arguments: Sequence[str]) -> bool:
    no_codegen = {"-E", "-M", "-MM", "-emit-llvm", "-fsyntax-only"}
    driver_only = {
        "--help",
        "--version",
        "-###",
        "-cc1",
        "-ccc-print-phases",
        "-dumpmachine",
        "-print-resource-dir",
    }
    return not any(
        option in no_codegen or option in driver_only
        for _, option, _ in clang_argument_groups(arguments)
    )


def expand_response_files(
    clang: Path, opt: Path, arguments: Sequence[str], environment: dict[str, str]
) -> list[str]:
    if not any(argument.startswith("@") for argument in arguments):
        return list(arguments)

    driver_mode = "cl" if clang.stem.lower() in {"clang-cl", "cl"} else "gcc"
    for argument in arguments:
        if argument.startswith("--driver-mode="):
            driver_mode = argument.partition("=")[2]
    quoting = "windows" if driver_mode == "cl" else "posix"
    try:
        result = subprocess.run(
            [os.fspath(opt), "--a2mba-expand-args", quoting, *arguments],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        fail(f"could not expand compiler response files: {error}")
    if result.returncode != 0:
        fail(f"could not expand compiler response files: {result.stderr.strip()}")
    try:
        expanded = json.loads(result.stdout)
    except json.JSONDecodeError:
        fail("a2mba-opt returned invalid compiler arguments")
    if not isinstance(expanded, list) or any(
        not isinstance(argument, str) for argument in expanded
    ):
        fail("a2mba-opt returned invalid compiler arguments")
    for argument in expanded:
        if argument.startswith("@"):
            fail(f"response file could not be read: {argument[1:]}")
    return expanded


def source_argument_indices(arguments: Sequence[str]) -> list[int]:
    sources: list[int] = []
    forced_language = False
    working_directory = clang_working_directory(arguments)

    for index, option, tokens in clang_argument_groups(arguments):
        if option is not None:
            if option == "-x":
                forced_language = tokens[1] != "none"
            elif option.startswith("-x") and len(option) > 2:
                forced_language = option[2:] != "none"
            continue
        argument = tokens[0]
        if argument == "-":
            sources.append(index)
            continue

        suffix = Path(argument).suffix.lower()
        if suffix in SOURCE_SUFFIXES or (
            forced_language and (working_directory / argument).is_file()
        ):
            sources.append(index)
    return sources


def clang_working_directory(arguments: Sequence[str]) -> Path:
    directory = Path.cwd()
    for _, option, tokens in clang_argument_groups(arguments):
        if option == "-working-directory":
            directory = Path(tokens[1])
        elif option is not None and option.startswith("-working-directory="):
            directory = Path(option.partition("=")[2])
    return directory.absolute()


def strip_output_and_action(
    arguments: Sequence[str], source_indices: set[int], kept_source: int
) -> list[str]:
    stripped: list[str] = []
    for index, option, tokens in clang_argument_groups(arguments):
        if index in source_indices and index != kept_source:
            continue
        if option is None:
            stripped.extend(tokens)
            continue
        if option in {"-o", "--output", "-c", "-S", "-emit-llvm"}:
            continue
        if option.startswith("--output="):
            continue
        if (
            option.startswith("-o")
            and len(option) > 2
            and not option.startswith(("-objc", "-object"))
        ):
            continue
        stripped.extend(tokens)
    return stripped


def clang_output(arguments: Sequence[str]) -> str | None:
    output = None
    for _, argument, tokens in clang_argument_groups(arguments):
        if argument is None:
            continue
        if argument in {"-o", "--output"}:
            output = tokens[1]
        elif argument.startswith("--output="):
            output = argument.partition("=")[2]
        elif (
            argument.startswith("-o")
            and len(argument) > 2
            and not argument.startswith(("-objc", "-object"))
        ):
            output = argument[2:]
    return output


def dependency_arguments(arguments: Sequence[str], source_index: int) -> list[str]:
    options = [option for _, option, _ in clang_argument_groups(arguments) if option]
    if not any(option in {"-MD", "-MMD"} for option in options):
        return []

    source = arguments[source_index]
    output = clang_output(arguments)
    target = output if output is not None else f"{Path(source).stem}.o"
    added = []
    if not any(option.startswith("-MF") for option in options):
        added.extend(["-MF", os.fspath(Path(target).with_suffix(".d"))])
    if not any(
        argument in {"-MT", "-MQ"}
        or (argument.startswith(("-MT", "-MQ")) and len(argument) > 3)
        for argument in options
    ):
        added.extend(["-MQ", target])
    return added


def final_codegen_arguments(
    arguments: Sequence[str], protected_bitcode: dict[int, Path]
) -> list[str]:
    final_arguments = ["-Qunused-arguments", "-Xclang", "-disable-llvm-passes"]
    dependency_flags = {"-MD", "-MMD", "-MG", "-MP"}
    dependency_values = {"-MF", "-MJ", "-MQ", "-MT"}

    for index, option, tokens in clang_argument_groups(arguments):
        if index in protected_bitcode:
            final_arguments.append(os.fspath(protected_bitcode[index]))
            continue
        if option is None:
            final_arguments.extend(tokens)
            continue
        if option in dependency_values or option == "-x":
            continue
        if option in dependency_flags:
            continue
        if option.startswith("-x") and len(option) > 2:
            continue
        if option.startswith(("-MF", "-MJ", "-MQ", "-MT")) and len(option) > 3:
            continue
        final_arguments.extend(tokens)
    return final_arguments


def run_staged_compile(
    clang: Path,
    opt: Path,
    plugin: Path | None,
    clang_arguments: Sequence[str],
    source_indices: Sequence[int],
    environment: dict[str, str],
) -> int:
    with tempfile.TemporaryDirectory(prefix="a2mba-clang-") as temporary:
        temporary_path = Path(temporary)
        commands: list[list[str]] = []
        protected_by_source: dict[int, Path] = {}
        source_index_set = set(source_indices)
        options = {option for _, option, _ in clang_argument_groups(clang_arguments)}
        working_directory = clang_working_directory(clang_arguments)
        llvm_output = "-emit-llvm" in options
        if llvm_output and len(source_indices) != 1:
            print(
                "a2mba-clang: error: staged LLVM output accepts one source file",
                file=sys.stderr,
            )
            return 2

        for sequence, source_index in enumerate(source_indices):
            input_bitcode = temporary_path / f"input-{sequence}.bc"
            protected_bitcode = temporary_path / f"protected-{sequence}.bc"
            if llvm_output:
                requested_output = clang_output(clang_arguments)
                if requested_output:
                    protected_bitcode = Path(requested_output)
                else:
                    source = clang_arguments[source_index]
                    suffix = ".ll" if "-S" in options else ".bc"
                    protected_bitcode = Path(Path(source).name).with_suffix(suffix)
                if working_directory != Path.cwd() and protected_bitcode != Path("-"):
                    protected_bitcode = working_directory / protected_bitcode
            protected_by_source[source_index] = protected_bitcode
            commands.append(
                [
                    os.fspath(clang),
                    "-Qunused-arguments",
                    *strip_output_and_action(
                        clang_arguments, source_index_set, source_index
                    ),
                    *dependency_arguments(clang_arguments, source_index),
                    "-emit-llvm",
                    "-c",
                    "-o",
                    os.fspath(input_bitcode),
                ]
            )
            opt_command = [os.fspath(opt)]
            if plugin is not None:
                opt_command.append(f"-load-pass-plugin={plugin}")
            opt_command.extend(["-passes=a2mba"])
            if llvm_output and "-S" in options:
                opt_command.append("-S")
            opt_command.extend(
                [os.fspath(input_bitcode), "-o", os.fspath(protected_bitcode)]
            )
            commands.append(opt_command)
        if not llvm_output:
            final_arguments = final_codegen_arguments(
                clang_arguments, protected_by_source
            )
            if clang_output(clang_arguments) is None and any(
                action in options for action in ("-c", "-S")
            ):
                source = clang_arguments[source_indices[0]]
                suffix = ".s" if "-S" in options else ".o"
                final_arguments.extend(["-o", Path(source).stem + suffix])
            commands.append(
                [
                    os.fspath(clang),
                    *final_arguments,
                ]
            )
        for sequence, command in enumerate(commands):
            if (
                os.name == "nt"
                and command[0] == os.fspath(clang)
                and len(subprocess.list2cmdline(command)) > 30000
            ):
                response_file = temporary_path / f"command-{sequence}.rsp"
                quoted = [
                    '"' + argument.replace("\\", "\\\\").replace('"', '\\"') + '"'
                    for argument in command[1:]
                ]
                response_file.write_text("\n".join(quoted), encoding="utf-8")
                command = [os.fspath(clang), "--rsp-quoting=posix", f"@{response_file}"]
            try:
                result = subprocess.run(command, check=False, env=environment)
            except OSError as error:
                print(
                    f"a2mba-clang: error: could not execute {command[0]}: {error}",
                    file=sys.stderr,
                )
                return 2
            if result.returncode != 0:
                return relay_child_status(result.returncode)
    return 0


def run_doctor(
    clang: Path,
    opt: Path | None,
    plugin: Path | None,
    version: tuple[int, int, int],
    environment: dict[str, str],
) -> int:
    commands: list[list[str]]
    doctor_input = "unsigned a2mba_doctor(unsigned a, unsigned b) { return a + b; }\n"
    temporary: tempfile.TemporaryDirectory[str] | None = None
    if os.name == "nt":
        if opt is None:
            print("a2mba-clang: error: opt is required on Windows", file=sys.stderr)
            return 1
        temporary = tempfile.TemporaryDirectory(prefix="a2mba-doctor-")
        temporary_path = Path(temporary.name)
        input_bitcode = temporary_path / "input.bc"
        protected_bitcode = temporary_path / "protected.bc"
        output_object = temporary_path / "output.obj"
        opt_command = [os.fspath(opt)]
        if plugin is not None:
            opt_command.append(f"-load-pass-plugin={plugin}")
        opt_command.extend(
            [
                "-passes=a2mba",
                os.fspath(input_bitcode),
                "-o",
                os.fspath(protected_bitcode),
            ]
        )
        commands = [
            [
                os.fspath(clang),
                "-O1",
                "-x",
                "c",
                "-emit-llvm",
                "-c",
                "-",
                "-o",
                os.fspath(input_bitcode),
            ],
            opt_command,
            [
                os.fspath(clang),
                "-O1",
                "-Xclang",
                "-disable-llvm-passes",
                "-c",
                os.fspath(protected_bitcode),
                "-o",
                os.fspath(output_object),
            ],
        ]
    else:
        commands = [
            [
                os.fspath(clang),
                f"-fpass-plugin={plugin}",
                "-O1",
                "-x",
                "c",
                "-S",
                "-emit-llvm",
                "-",
                "-o",
                os.devnull,
            ]
        ]

    doctor_environment = environment.copy()
    doctor_environment["A2MBA_OPTIONS"] = (
        "mode=verified;level=light;seed=1;functions=all;"
        "transform=rule-explosion;probability=100;depth=1;stats=false"
    )
    failure_output = ""
    try:
        plugin_loaded = True
        for index, command in enumerate(commands):
            result = subprocess.run(
                command,
                check=False,
                input=doctor_input if index == 0 else None,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                errors="replace",
                env=doctor_environment,
                timeout=30,
            )
            if result.returncode != 0:
                plugin_loaded = False
                failure_output = result.stderr
                break
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"a2mba-clang: error: plugin load check failed: {error}", file=sys.stderr)
        return 1
    finally:
        if temporary:
            temporary.cleanup()

    version_text = ".".join(str(part) for part in version)
    target = clang_target(clang)
    target_supported = is_supported_target(target)
    compatible = plugin_loaded and target_supported
    print("A2MBA doctor")
    print(f"clang: {clang}")
    print(f"version: {version_text}")
    print(f"target: {target}")
    if plugin:
        print(f"plugin: {plugin}")
    if opt:
        print(f"driver: {opt}")
        print("pipeline: staged")
    print(f"pass load: {'ok' if plugin_loaded else 'failed'}")
    print(f"AAMBA: {'supported' if target_supported else 'unsupported'}")
    print(f"AGT: {'supported' if target_supported else 'unsupported'}")
    print("LTO: disabled")
    print(f"compatible: {'yes' if compatible else 'no'}")
    if not plugin_loaded and failure_output:
        print(failure_output.rstrip(), file=sys.stderr)
    elif not target_supported:
        print(
            "a2mba-clang: error: A2MBA v0.1 requires an x86-64 Linux or Windows target",
            file=sys.stderr,
        )
    return 0 if compatible else 1


def relay_child_status(returncode: int) -> int:
    if returncode >= 0 or os.name == "nt":
        return returncode

    signum = -returncode
    signal.signal(signum, signal.SIG_DFL)
    os.kill(os.getpid(), signum)
    return 128 + signum


def main(argv: Sequence[str] | None = None) -> int:
    raw_arguments = list(argv if argv is not None else sys.argv[1:])
    if raw_arguments[:1] == ["doctor"]:
        raw_arguments[0] = "--doctor"

    parser = build_parser()
    arguments, clang_arguments = parse_arguments(parser, raw_arguments)
    if arguments.doctor and clang_arguments:
        parser.error("--doctor does not accept Clang arguments")

    environment = os.environ.copy()
    opt: Path | None = None
    plugin: Path | None = None
    try:
        clang = find_clang(arguments.clang, environment)
        major, minor, patch, _ = clang_version(clang)
        version = (major, minor, patch)
        if os.name == "nt":
            if arguments.plugin:
                fail("--plugin is not used on Windows; pass --opt with a2mba-opt.exe")
            opt = find_opt(arguments.opt, environment, clang, version)
        else:
            plugin = find_plugin(arguments.plugin, environment, version)
    except WrapperError as error:
        print(f"a2mba-clang: error: {error}", file=sys.stderr)
        return 2

    environment["A2MBA_OPTIONS"] = make_options(arguments)
    if arguments.doctor:
        return run_doctor(clang, opt, plugin, version, environment)

    clang_options: set[str] = set()
    if os.name == "nt":
        assert opt is not None
        try:
            clang_arguments = expand_response_files(
                clang, opt, clang_arguments, environment
            )
            clang_options = {
                option
                for _, option, _ in clang_argument_groups(clang_arguments)
                if option is not None
            }
        except WrapperError as error:
            print(f"a2mba-clang: error: {error}", file=sys.stderr)
            return 2

    if os.name == "nt" and (
        machine_codegen_requested(clang_arguments) or "-emit-llvm" in clang_options
    ):
        if any(
            argument == "-flto" or argument.startswith("-flto=")
            for argument in clang_options
        ):
            print("a2mba-clang: error: LTO is not supported", file=sys.stderr)
            return 2

        source_indices = source_argument_indices(clang_arguments)
        if optimization_enabled(clang_arguments) and source_indices:
            if len(source_indices) > 1 and any(
                action in clang_options for action in ("-c", "-S")
            ):
                print(
                    "a2mba-clang: error: compile-only Windows invocations accept "
                    "one source file; compile each translation unit separately",
                    file=sys.stderr,
                )
                return 2
            assert opt is not None
            return run_staged_compile(
                clang,
                opt,
                None,
                clang_arguments,
                source_indices,
                environment,
            )

    if os.name == "nt":
        try:
            result = subprocess.run(
                [os.fspath(clang), *clang_arguments], check=False, env=environment
            )
        except OSError as error:
            print(
                f"a2mba-clang: error: could not execute Clang: {error}", file=sys.stderr
            )
            return 2
        return relay_child_status(result.returncode)

    assert plugin is not None
    try:
        result = subprocess.run(
            [os.fspath(clang), f"-fpass-plugin={plugin}", *clang_arguments],
            check=False,
            env=environment,
        )
    except OSError as error:
        print(f"a2mba-clang: error: could not execute Clang: {error}", file=sys.stderr)
        return 2
    return relay_child_status(result.returncode)


if __name__ == "__main__":
    raise SystemExit(main())
