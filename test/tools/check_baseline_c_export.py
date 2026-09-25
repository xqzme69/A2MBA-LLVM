from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

import baseline  # noqa: E402


def main():
    clang = sys.argv[1]
    source = ["typedef unsigned long long uint64_t;", "typedef long long int64_t;"]
    checks = []
    case = 0
    for operator in ("shl", "lshr", "ashr"):
        for amount in (0, 1, 63):
            shifted = baseline.Expression.binary(
                operator,
                baseline.Expression.var("x"),
                baseline.Expression.const(amount),
            )
            expression = baseline.Expression.binary(
                "add", shifted, baseline.Expression.const(-1)
            )
            name = f"{operator}_{amount}"
            source.extend(
                (
                    f"uint64_t flat_{name}(uint64_t x) {{",
                    f"  return {expression.render(c_expression=True)};",
                    "}",
                    f"uint64_t dag_{name}(uint64_t x) {{",
                )
            )
            statements, result = baseline.render_c_dag(expression)
            source.extend((*statements, f"  return {result};", "}"))
            for value in (0, 1, 1 << 63, (1 << 64) - 1):
                expected = baseline.c_constant(expression.evaluate({"x": value}, 64))
                argument = baseline.c_constant(value)
                for form in ("flat", "dag"):
                    case += 1
                    checks.append(
                        f"  if ({form}_{name}({argument}) != {expected}) return {case};"
                    )
    source.extend(("int main(void) {", *checks, "  return 0;", "}"))

    with tempfile.TemporaryDirectory(prefix="a2mba-c-export-") as temporary:
        root = Path(temporary)
        input_path = root / "export.c"
        input_path.write_text("\n".join(source), encoding="utf-8")
        for optimization in ("-O0", "-O3"):
            executable = root / f"export{optimization}.exe"
            subprocess.run(
                [clang, optimization, str(input_path), "-o", str(executable)],
                check=True,
                timeout=30,
            )
            subprocess.run([str(executable)], check=True, timeout=10)
    print(f"PASS: {case} C export comparisons at each of -O0 and -O3")


if __name__ == "__main__":
    main()
