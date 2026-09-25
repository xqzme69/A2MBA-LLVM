import argparse
import pathlib
import re


SSA_NAME = re.compile(r"%[-a-zA-Z$._0-9]+")
DEFINITION = re.compile(r"^\s*(%[-a-zA-Z$._0-9]+)\s*=\s*(.*)$")
MULTIPLICATION = re.compile(
    r"\bmul(?:\s+(?:nuw|nsw))*\s+i(?:32|64)\s+([^,]+),\s*([^,\s]+)"
)


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=pathlib.Path)
    parser.add_argument("--function", required=True)
    parser.add_argument("--minimum", type=int, default=1)
    return parser.parse_args()


def read_function(ir, function_name):
    header = re.search(
        rf"^define\b.*@{re.escape(function_name)}\((.*?)\).*?\{{\s*$",
        ir,
        re.MULTILINE,
    )
    if header is None:
        raise ValueError(f"function not found: {function_name}")

    body_start = header.end()
    body_end = ir.find("\n}", body_start)
    if body_end < 0:
        raise ValueError(f"unterminated function: {function_name}")
    return SSA_NAME.findall(header.group(1)), ir[body_start:body_end].splitlines()


def main():
    args = parse_arguments()
    arguments, body = read_function(
        args.path.read_text(encoding="utf-8"), args.function
    )
    dependencies = {argument: {argument} for argument in arguments}
    nonlinear_multiplications = 0

    for line in body:
        definition = DEFINITION.match(line)
        if definition is None:
            continue

        name, expression = definition.groups()
        multiplication = MULTIPLICATION.search(expression)
        if multiplication is not None:
            left_dependencies = set()
            right_dependencies = set()
            for value in SSA_NAME.findall(multiplication.group(1)):
                left_dependencies.update(dependencies.get(value, set()))
            for value in SSA_NAME.findall(multiplication.group(2)):
                right_dependencies.update(dependencies.get(value, set()))
            if left_dependencies and right_dependencies:
                nonlinear_multiplications += 1

        value_dependencies = set()
        for value in SSA_NAME.findall(expression):
            value_dependencies.update(dependencies.get(value, set()))
        dependencies[name] = value_dependencies

    if nonlinear_multiplications < args.minimum:
        raise SystemExit(
            f"{args.function}: expected at least {args.minimum} variable-by-variable "
            f"multiplications, found {nonlinear_multiplications}"
        )
    print(
        f"{args.function}: {nonlinear_multiplications} variable-by-variable "
        "multiplications survived"
    )


if __name__ == "__main__":
    main()
