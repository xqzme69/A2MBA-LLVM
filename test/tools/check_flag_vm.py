import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


CALL = re.compile(
    r"(?P<name>%a2mba\.flagvm\d*) = call \{ i(?P<width>32|64), "
    r'i(?P=width) \} asm "(?P<asm>[^"]+)", "(?P<constraints>[^"]+)"'
)


def flag_kind(assembly):
    if "adc" in assembly or "sbb" in assembly or "setb " in assembly:
        return 0
    if "$$4" in assembly and "shr" in assembly:
        return 2
    condition = re.search(r"set(n?[czsop]) ", assembly)
    if not condition:
        raise ValueError(f"unknown flag materialization: {assembly}")
    return {"c": 0, "p": 1, "z": 3, "s": 4, "o": 5}[condition.group(1).lstrip("n")]


def read_sites(paths):
    sites = []
    triple = None
    for path in paths:
        lines = path.read_text(encoding="utf-8").splitlines()
        for line in lines:
            if line.startswith("target triple = "):
                current = line
                if triple is not None and current != triple:
                    raise ValueError("input triples differ")
                triple = current
        for index, line in enumerate(lines):
            match = CALL.search(line)
            if not match:
                continue
            if index + 2 >= len(lines):
                raise ValueError("flag VM outputs are missing")
            sum_output = re.search(
                re.escape(match["name"]) + r", ([01])", lines[index + 1]
            )
            flag_output = re.search(
                re.escape(match["name"]) + r", ([01])", lines[index + 2]
            )
            if not sum_output or not flag_output or sum_output[1] == flag_output[1]:
                raise ValueError("flag VM output mapping is invalid")
            sites.append(
                (
                    int(match["width"]),
                    match["asm"],
                    match["constraints"],
                    int(sum_output[1]),
                    int(flag_output[1]),
                    flag_kind(match["asm"]),
                )
            )
    if not sites or triple is None:
        raise ValueError("no flag VM sites found")
    forms = {kind for _, _, _, _, _, kind in sites}
    assemblies = [assembly for _, assembly, _, _, _, _ in sites]
    layouts = {sum_output for _, _, _, sum_output, _, _ in sites}
    if forms != set(range(6)) or layouts != {0, 1}:
        raise ValueError("test seeds do not cover all flags and output layouts")
    if not all(
        any(opcode in asm for asm in assemblies) for opcode in ("adc", "sbb", "cmp")
    ):
        raise ValueError("test seeds do not cover all carry forms")
    parity = [assembly for _, assembly, _, _, _, kind in sites if kind == 1]
    auxiliary = [assembly for _, assembly, _, _, _, kind in sites if kind == 2]
    if not any("setp " in assembly for assembly in parity) or not any(
        "setnp " in assembly for assembly in parity
    ):
        raise ValueError("test seeds do not cover both parity forms")
    if not any("$$16" in assembly for assembly in auxiliary) or not any(
        "$$1," in assembly for assembly in auxiliary
    ):
        raise ValueError("test seeds do not cover both auxiliary-carry forms")
    return triple, sites


def probe_ir(triple, sites):
    lines = [triple, ""]
    for index, (width, assembly, constraints, sum_output, flag_output, _) in enumerate(
        sites
    ):
        for role, output in (("sum", sum_output), ("flag", flag_output)):
            lines.extend(
                (
                    f"define i{width} @vm_{index}_{role}(i{width} %x, i{width} %y) {{",
                    f'  %pair = call {{ i{width}, i{width} }} asm "{assembly}", '
                    f'"{constraints}"(i{width} %x, i{width} %y)',
                    f"  %result = extractvalue {{ i{width}, i{width} }} %pair, {output}",
                    f"  ret i{width} %result",
                    "}",
                    "",
                )
            )
    return "\n".join(lines)


def probe_c(sites):
    lines = [
        "#include <stdint.h>",
        "#include <stdio.h>",
        "typedef uint32_t (*fn32)(uint32_t, uint32_t);",
        "typedef uint64_t (*fn64)(uint64_t, uint64_t);",
        "struct site32 { fn32 sum, flag; unsigned kind; };",
        "struct site64 { fn64 sum, flag; unsigned kind; };",
        "static unsigned even_parity(uint8_t value) {",
        "  unsigned odd = 0;",
        "  for (unsigned bit = 0; bit != 8; ++bit) odd ^= (value >> bit) & 1;",
        "  return odd ^ 1;",
        "}",
    ]
    for index, (width, *_rest) in enumerate(sites):
        ctype = f"uint{width}_t"
        lines.append(f"extern {ctype} vm_{index}_sum({ctype}, {ctype});")
        lines.append(f"extern {ctype} vm_{index}_flag({ctype}, {ctype});")
    for width in (32, 64):
        entries = [
            f"{{ vm_{index}_sum, vm_{index}_flag, {kind} }}"
            for index, (site_width, _, _, _, _, kind) in enumerate(sites)
            if site_width == width
        ]
        if not entries:
            raise ValueError(f"no i{width} sites")
        lines.append(f"static const struct site{width} sites{width}[] = {{")
        lines.extend(f"  {entry}," for entry in entries)
        lines.append("};")
    lines.extend(
        (
            "static unsigned expected32(unsigned kind, uint32_t x, uint32_t y) {",
            "  uint32_t sum = x + y;",
            "  switch (kind) {",
            "  case 0: return sum < x;",
            "  case 1: return even_parity((uint8_t)sum);",
            "  case 2: return ((x ^ y ^ sum) >> 4) & 1;",
            "  case 3: return sum == 0;",
            "  case 4: return sum >> 31;",
            "  default: return ((~(x ^ y) & (x ^ sum)) >> 31) & 1;",
            "  }",
            "}",
            "static unsigned expected64(unsigned kind, uint64_t x, uint64_t y) {",
            "  uint64_t sum = x + y;",
            "  switch (kind) {",
            "  case 0: return sum < x;",
            "  case 1: return even_parity((uint8_t)sum);",
            "  case 2: return (unsigned)(((x ^ y ^ sum) >> 4) & 1);",
            "  case 3: return sum == 0;",
            "  case 4: return (unsigned)(sum >> 63);",
            "  default: return (unsigned)((~(x ^ y) & (x ^ sum)) >> 63) & 1;",
            "  }",
            "}",
            "static int check(uint64_t x, uint64_t y) {",
            "  uint32_t x32 = (uint32_t)x, y32 = (uint32_t)y;",
            "  for (unsigned i = 0; i < sizeof(sites32) / sizeof(sites32[0]); ++i) {",
            "    const struct site32 *site = &sites32[i];",
            "    if (site->sum(x32, y32) != (uint32_t)(x32 + y32) ||",
            "        site->flag(x32, y32) != expected32(site->kind, x32, y32)) {",
            '      fprintf(stderr, "i32 flag VM mismatch at site %u\\n", i);',
            "      return 1;",
            "    }",
            "  }",
            "  for (unsigned i = 0; i < sizeof(sites64) / sizeof(sites64[0]); ++i) {",
            "    const struct site64 *site = &sites64[i];",
            "    if (site->sum(x, y) != x + y ||",
            "        site->flag(x, y) != expected64(site->kind, x, y)) {",
            '      fprintf(stderr, "i64 flag VM mismatch at site %u\\n", i);',
            "      return 1;",
            "    }",
            "  }",
            "  return 0;",
            "}",
            "int main(void) {",
            "  const uint64_t edge[] = { 0, 1, 15, 16, UINT32_MAX, UINT64_MAX,",
            "    UINT64_C(0x7fffffff), UINT64_C(0x80000000),",
            "    UINT64_C(0x7fffffffffffffff), UINT64_C(0x8000000000000000) };",
            "  for (unsigned i = 0; i < sizeof(edge) / sizeof(edge[0]); ++i)",
            "    for (unsigned j = 0; j < sizeof(edge) / sizeof(edge[0]); ++j)",
            "      if (check(edge[i], edge[j])) return 1;",
            "  uint64_t state = UINT64_C(0x6a09e667f3bcc909);",
            "  for (unsigned i = 0; i < 1024; ++i) {",
            "    state = state * UINT64_C(6364136223846793005) + 1;",
            "    uint64_t x = state;",
            "    state = state * UINT64_C(6364136223846793005) + 1;",
            "    if (check(x, state)) return 1;",
            "  }",
            "  return 0;",
            "}",
        )
    )
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("inputs", nargs="+", type=Path)
    args = parser.parse_args()
    triple, sites = read_sites(args.inputs)
    with tempfile.TemporaryDirectory(prefix="a2mba-flag-vm-") as temporary:
        directory = Path(temporary)
        ir = directory / "probe.ll"
        source = directory / "probe.c"
        executable = directory / ("probe.exe" if os.name == "nt" else "probe")
        ir.write_text(probe_ir(triple, sites), encoding="utf-8")
        source.write_text(probe_c(sites), encoding="utf-8")
        subprocess.run([args.clang, "-O3", ir, source, "-o", executable], check=True)
        subprocess.run([executable], check=True)
    print(f"PASS: {len(sites)} native flag VM sites")


if __name__ == "__main__":
    main()
