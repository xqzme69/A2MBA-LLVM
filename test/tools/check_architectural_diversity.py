import argparse
import pathlib
import re


ASSEMBLY = re.compile(r'asm sideeffect "([^"]+)"')


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=pathlib.Path)
    parser.add_argument("--kind", choices=("adc", "sbb"), required=True)
    parser.add_argument("--expected", type=int, required=True)
    parser.add_argument("--minimum-skeletons", type=int, default=4)
    parser.add_argument("--minimum-setups", type=int, default=3)
    return parser.parse_args()


def carry_setup(assembly):
    if "clc\\0A\\09cmc" in assembly:
        return "clc-cmc"
    if "btq $$0" in assembly or "btl $$0" in assembly:
        return "bt"
    if re.search(r"cmp[ql].*\\0A\\09cmc", assembly):
        return "cmp-cmc"
    if re.search(r"(^|\\0A\\09)stc(\\0A\\09|$)", assembly):
        return "stc"
    raise ValueError(f"unknown carry setup: {assembly}")


def main():
    args = parse_arguments()
    ir = args.path.read_text(encoding="utf-8")
    assemblies = [
        assembly
        for assembly in ASSEMBLY.findall(ir)
        if f"{args.kind}q " in assembly or f"{args.kind}l " in assembly
    ]
    if len(assemblies) != args.expected:
        raise SystemExit(
            f"expected {args.expected} {args.kind.upper()} gadgets, found {len(assemblies)}"
        )

    skeletons = set(assemblies)
    setups = {carry_setup(assembly) for assembly in assemblies}
    flag_modes = {
        "saved" if "pushfq" in assembly else "clobbered" for assembly in assemblies
    }
    if len(skeletons) < args.minimum_skeletons:
        raise SystemExit(
            f"expected at least {args.minimum_skeletons} skeletons, found {len(skeletons)}"
        )
    if len(setups) < args.minimum_setups:
        raise SystemExit(
            f"expected at least {args.minimum_setups} carry setups, found {sorted(setups)}"
        )
    if flag_modes != {"saved", "clobbered"}:
        raise SystemExit(f"expected both flag modes, found {sorted(flag_modes)}")
    print(
        f"{args.kind.upper()}: {len(skeletons)} skeletons, "
        f"setups={','.join(sorted(setups))}, flag-modes={','.join(sorted(flag_modes))}"
    )


if __name__ == "__main__":
    main()
