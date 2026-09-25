import operator
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

import baseline  # noqa: E402


def reference_checksum(iterations):
    mask = (1 << 64) - 1
    state = 81985529216486895
    checksum = 1469598103934665603
    operations = (
        operator.add,
        operator.xor,
        operator.sub,
        operator.or_,
        operator.mul,
        operator.and_,
    )
    for iteration in range(iterations):
        position = 0
        for length in (1, 2, 3, 5, 7):
            for index in range(2):
                operands = (iteration, position + 3, position * 17 + 5)
                for step in range(length):
                    operation = operations[(index + step) % len(operations)]
                    state = operation(state, operands[step % 3]) & mask
                checksum = ((checksum ^ state) * 1099511628211) & mask
                position += 1
        state ^= iteration
        checksum ^= iteration
    return f"{checksum:016x}{os.linesep}".encode("ascii")


def main():
    clang = sys.argv[1]
    target = (
        baseline.run([clang, "-print-target-triple"], timeout=10)
        .stdout.decode()
        .strip()
    )
    iterations = 19
    expected = reference_checksum(iterations)
    source = baseline.make_module(
        2, runtime_iterations=iterations, target_triple=target
    )
    with tempfile.TemporaryDirectory(prefix="a2mba-workload-") as temporary:
        root = Path(temporary)
        input_path = root / "workload.ll"
        input_path.write_text(source, encoding="utf-8")
        for optimization in ("-O0", "-O3"):
            executable = root / f"workload{optimization}.exe"
            subprocess.run(
                [clang, optimization, str(input_path), "-o", str(executable)],
                check=True,
                timeout=60,
            )
            baseline.run_workload(executable, expected)

        input_path.write_text(
            source.replace("add i64 %x, %y", "xor i64 %x, %y", 1), encoding="utf-8"
        )
        executable = root / "broken.exe"
        subprocess.run(
            [clang, "-O3", str(input_path), "-o", str(executable)],
            check=True,
            timeout=60,
        )
        try:
            baseline.run_workload(executable, expected)
        except baseline.BaselineError as error:
            if "checksum differs" not in str(error):
                raise
        else:
            raise AssertionError("the benchmark accepted an incorrect result")
    print("PASS: workload checksum at -O0/-O3 and rejection of an incorrect result")


if __name__ == "__main__":
    main()
