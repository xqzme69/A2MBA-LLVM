#!/usr/bin/env python3
import json
from pathlib import Path
import subprocess
import sys
import tempfile

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary_directory:
        temporary_root = Path(temporary_directory)
        invalid_specification = {
            "schema": 1,
            "widths": [8, 16, 32, 64],
            "rules": [
                {
                    "name": "intentionally_wrong",
                    "lhs": ["add", "$x", "$y"],
                    "rhs": ["xor", "$x", "$y"],
                }
            ],
        }
        specification_path = temporary_root / "bad.json"
        generated_header = temporary_root / "bad.inc"
        specification_path.write_text(
            json.dumps(invalid_specification), encoding="utf-8"
        )
        completed = subprocess.run(
            [
                sys.executable,
                str(REPOSITORY_ROOT / "scripts/verify_hybrid_rules.py"),
                "--spec",
                str(specification_path),
                "--header",
                str(generated_header),
                "--report",
                str(temporary_root / "bad-report.json"),
            ],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        if (
            completed.returncode == 0
            or generated_header.exists()
            or "sat" not in completed.stderr
        ):
            print(completed.stdout, completed.stderr)
            raise SystemExit("invalid rule was not rejected as expected")
    print("PASS: SAT rule rejected before C++ generation")


if __name__ == "__main__":
    main()
