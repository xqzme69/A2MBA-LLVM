"""A corrupted carry schedule must never yield an admitted production template."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]

spec = json.loads(
    (root / "lib/HybridCore/coordinates.json").read_text(encoding="utf-8")
)
spec["rules"]["packedRotatedAdd"]["steps"][-1][1] = "and"
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "broken.json"
    header = Path(tmp) / "rules.inc"
    path.write_text(json.dumps(spec), encoding="utf-8")
    run = subprocess.run(
        [sys.executable, root / "scripts/verify_coordinate_rules.py", path, header],
        capture_output=True,
        text=True,
    )
    if run.returncode == 0 or ": sat" not in run.stderr or header.exists():
        raise RuntimeError("invalid carry compensation was not rejected by Z3")
