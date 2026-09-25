import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def quote_response(arguments):
    return "\n".join(
        '"' + argument.replace("\\", "\\\\").replace('"', '\\"') + '"'
        for argument in arguments
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--wrapper",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "tools" / "a2mba-clang.py",
    )
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="a2mba-wrapper-") as temporary:
        directory = Path(temporary)
        sources = directory / "sources ü"
        sources.mkdir()
        source = sources / "mix.c"
        source_text = (
            '__attribute__((annotate("a2mba"), noinline))\n'
            "unsigned long long mix(unsigned long long x, unsigned long long y) {\n"
            "  return ((x + y) ^ 0x9e3779b97f4a7c15ULL) - y;\n"
            "}\n"
        )
        source.write_text(source_text, encoding="utf-8")
        caller = sources / "caller.c"
        caller.write_text(
            "unsigned long long mix(unsigned long long, unsigned long long);\n"
            "int main(void) {\n"
            "  for (unsigned long long x = 0; x < 256; ++x) {\n"
            "    unsigned long long y = ~x;\n"
            "    if (mix(x, y) != (((x + y) ^ 0x9e3779b97f4a7c15ULL) - y))\n"
            "      return 1;\n"
            "  }\n"
            "  return 0;\n"
            "}\n",
            encoding="utf-8",
        )

        def compile_case(clang_arguments, *, input_text=None, error=None):
            result = subprocess.run(
                [
                    sys.executable,
                    str(arguments.wrapper),
                    "--level",
                    "heavy",
                    "--hybrid",
                    "ir",
                    "--hybrid-region",
                    "stateful",
                    "--seed",
                    "913",
                    "--stats",
                    *clang_arguments,
                ],
                cwd=directory,
                input=input_text,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=30,
            )
            if error is not None:
                if result.returncode == 0 or error not in result.stderr:
                    raise RuntimeError(f"expected {error}: {result.stderr}")
            elif result.returncode != 0 or "stateful regions: 1" not in result.stderr:
                raise RuntimeError(f"compile did not emit the region: {result.stderr}")
            return result

        compile_case(["-O3", "-S", "-emit-llvm", str(source)])
        llvm_output = directory / "mix.ll"
        assert "a2mba.region." in llvm_output.read_text(encoding="utf-8")
        assert not source.with_suffix(".ll").exists()

        compile_case(["-O3", "-c", "-MMD", str(source)])
        assert (directory / "mix.o").stat().st_size > 0
        dependencies = (directory / "mix.d").read_text(encoding="utf-8")
        assert "mix.o:" in dependencies and "input-0" not in dependencies

        compile_case(["-O3", "-c", "-o", "sentinel.o", "--", str(source)])
        assert (directory / "sentinel.o").stat().st_size > 0

        (directory / "-O0").mkdir()
        compile_case(["-O3", "-I", "-O0", "-c", str(source), "-o", "option-value.o"])
        assert (directory / "option-value.o").stat().st_size > 0

        for index, option_name in enumerate(
            ("--seed", "--stats", "--doctor", "--help")
        ):
            include_directory = directory / option_name
            include_directory.mkdir()
            (include_directory / "option.h").write_text(
                "#define OPTION_HEADER 1\n", encoding="utf-8"
            )
            output_name = f"wrapper-option-{index}.o"
            compile_case(
                [
                    "-O3",
                    "-I",
                    option_name,
                    "-include",
                    "option.h",
                    "-c",
                    str(source),
                    "-o",
                    output_name,
                ]
            )
            assert (directory / output_name).stat().st_size > 0

        working_directory = directory / "working Ω"
        working_directory.mkdir()
        (working_directory / "work-source.c").write_text(source_text, encoding="utf-8")
        (working_directory / "extensionless").write_text(source_text, encoding="utf-8")
        compile_case(
            [
                "-O3",
                "-working-directory",
                str(working_directory),
                "-S",
                "-emit-llvm",
                "work-source.c",
                "-o",
                "named.ll",
            ]
        )
        assert "a2mba.region." in (working_directory / "named.ll").read_text(
            encoding="utf-8"
        )
        assert not (directory / "named.ll").exists()

        compile_case(
            [
                "-O3",
                f"-working-directory={working_directory}",
                "-xc",
                "-S",
                "-emit-llvm",
                "extensionless",
            ]
        )
        assert "a2mba.region." in (working_directory / "extensionless.ll").read_text(
            encoding="utf-8"
        )
        assert not (directory / "extensionless.ll").exists()

        compile_case(
            [
                "-O3",
                "-working-directory",
                str(working_directory),
                "-xc",
                "-c",
                "-MMD",
                "extensionless",
                "-o",
                "relative.o",
            ]
        )
        assert (working_directory / "relative.o").stat().st_size > 0
        assert "relative.o:" in (working_directory / "relative.d").read_text(
            encoding="utf-8"
        )
        assert not (directory / "relative.o").exists()

        stdout_result = compile_case(
            [
                "-O3",
                "-working-directory",
                str(working_directory),
                "-xc",
                "-S",
                "-emit-llvm",
                "extensionless",
                "-o",
                "-",
            ]
        )
        assert "a2mba.region." in stdout_result.stdout
        assert not (working_directory / "-").exists()

        compile_case(
            [
                "-O3",
                "-working-directory",
                str(working_directory),
                "work-source.c",
                str(caller),
                "-o",
                "working.exe",
            ]
        )
        subprocess.run([str(working_directory / "working.exe")], check=True, timeout=10)

        option_source = directory / "-flto=thin"
        option_source.write_text(source_text, encoding="utf-8")
        compile_case(["-O3", "-xc", "-c", "-o", "option-path.o", "--", "-flto=thin"])
        assert (directory / "option-path.o").stat().st_size > 0
        compile_case(["-O3", str(source), "-o"], error="missing argument to -o")

        invalid_source = sources / "invalid.c"
        invalid_source.write_text(
            "unsigned broken(void) { return +; }\n", encoding="utf-8"
        )
        compile_case(
            ["-O3", "-c", str(invalid_source), "-o", "invalid.o"],
            error="expected expression",
        )
        assert not (directory / "invalid.o").exists()

        compile_case(
            ["--hybrid", "off", "-O3", "-c", str(source), "-o", "invalid-mode.o"],
            error="hybrid-region requires hybrid=ir or hybrid=native",
        )
        assert not (directory / "invalid-mode.o").exists()

        backend_failure = compile_case(
            ["-O3", "-c", str(source), "-o", "missing-directory/output.o"],
            error="unable to open output file",
        )
        assert "stateful regions: 1" in backend_failure.stderr
        assert not (directory / "missing-directory").exists()

        compile_case(["-O3", "-S", str(source)])
        assert (directory / "mix.s").stat().st_size > 0

        compile_case(["-O3", "-xc", "-", "-S", "-emit-llvm"], input_text=source_text)
        assert "a2mba.region." in (directory / "-.ll").read_text(encoding="utf-8")

        compile_case(["-O3", str(source), str(caller), "-o", "linked.exe"])
        subprocess.run([str(directory / "linked.exe")], check=True, timeout=10)

        nested = directory / "nested.rsp"
        nested.write_text(
            quote_response(["-O3", "-c", str(source), "-o", "response.o"]),
            encoding="utf-8",
        )
        response = directory / "compilation Ω.rsp"
        response.write_text(quote_response([f"@{nested}"]), encoding="utf-16")
        compile_case([f"@{response}"])
        assert (directory / "response.o").stat().st_size > 0

        response.write_text(
            subprocess.list2cmdline(["-O3", "-c", str(source), "-o", "windows.o"]),
            encoding="utf-8-sig",
        )
        compile_case(["--rsp-quoting=windows", f"@{response}"])
        assert (directory / "windows.o").stat().st_size > 0

        response.write_text(
            quote_response(
                ["-O3", "-c", str(source), "-o", "long.o"]
                + [f"-DCONFIG_{index}=1" for index in range(2500)]
            ),
            encoding="utf-8",
        )
        compile_case([f"@{response}"])
        assert (directory / "long.o").stat().st_size > 0

        response.write_text(
            quote_response(["-O3", "-flto", str(source)]), encoding="utf-8"
        )
        compile_case([f"@{response}"], error="LTO is not supported")
        response.write_text(quote_response([f"@{response}"]), encoding="utf-8")
        compile_case([f"@{response}"], error="recursive expansion")
        compile_case(["@missing.rsp"], error="response file could not be read")

    print(
        "PASS: staged outputs, working directories, dependencies, stdin, linking, and response files"
    )


if __name__ == "__main__":
    main()
