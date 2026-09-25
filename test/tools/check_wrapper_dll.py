import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    sources = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--wrapper", type=Path, default=sources.parents[1] / "tools" / "a2mba-clang.py"
    )
    parser.add_argument("--sdk", type=Path, default=sources.parents[1] / "sdk")
    arguments = parser.parse_args()
    clang = Path(os.environ["A2MBA_CLANG"]).with_name("clang++.exe")
    # The GNU driver can still add libcmt when the frontend selects the DLL CRT.
    common = [
        "-O3",
        "-std=c++20",
        "-fms-runtime-lib=dll",
        "-fexceptions",
        "-Wl,/nodefaultlib:libcmt",
    ]
    with tempfile.TemporaryDirectory(prefix="a2mba-dll-") as temporary:
        output = Path(temporary)
        host = output / "host.exe"
        subprocess.run(
            [
                str(clang),
                *common,
                str(sources / "stateful_dll_host.cpp"),
                "-o",
                str(host),
            ],
            check=True,
            timeout=60,
        )
        for index, mode in enumerate(("ir", "native")):
            library = output / f"stateful-{mode}.dll"
            command = [
                sys.executable,
                str(arguments.wrapper),
                "--clang",
                str(clang),
                "--level",
                "heavy",
                "--hybrid",
                mode,
                "--hybrid-region",
                "stateful",
                "--hybrid-layers",
                "context-random",
                "--seed",
                str(1051 + index),
                "--stats",
                *common,
                "-I",
                str(arguments.sdk),
                "-shared",
                str(sources / "stateful_dll.cpp"),
                str(sources / "stateful_dll_bridge.cpp"),
                "-o",
                str(library),
            ]
            result = subprocess.run(
                command, capture_output=True, text=True, errors="replace", timeout=120
            )
            regions = sum(
                int(count)
                for count in re.findall(r"stateful regions: (\d+)", result.stderr)
            )
            emitter = "IR" if mode == "ir" else "native"
            operations = sum(
                int(count)
                for count in re.findall(rf"hybrid {emitter}: (\d+)", result.stderr)
            )
            if result.returncode != 0 or regions != 3 or operations < 17:
                raise RuntimeError(
                    f"{mode} DLL did not emit all three regions:\n{result.stdout}\n{result.stderr}"
                )
            subprocess.run([str(host), str(library)], check=True, timeout=30)
    print(
        "PASS: stateful IR/native DLLs, separate C++ host, and eight-argument Win64 calls"
    )


if __name__ == "__main__":
    main()
