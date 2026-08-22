import os
import shlex

import lit.formats
import lit.util


config.name = "A2MBA"
config.test_format = lit.formats.ShTest(execute_external=False)
config.suffixes = [".ll", ".c", ".cpp", ".test"]
config.excludes = ["CMakeLists.txt", "lit.cfg.py", "tools"]
config.test_source_root = config.a2mba_source_dir
config.test_exec_root = config.a2mba_binary_dir


def quote(value):
    return shlex.quote(os.path.normpath(value))


def require_tool(name, override=""):
    path = (
        os.path.abspath(override)
        if override
        else lit.util.which(name, config.llvm_tools_dir)
    )
    if path is None:
        lit_config.fatal(
            f"required LLVM tool '{name}' was not found in {config.llvm_tools_dir}"
        )
    if not os.path.isfile(path):
        lit_config.fatal(f"configured LLVM tool '{name}' does not exist: {path}")
    return path


if not config.a2mba_plugin or not os.path.isfile(config.a2mba_plugin):
    lit_config.fatal("A2MBA plugin was not built; run the check-a2mba build target")
if not config.a2mba_selftest or not os.path.isfile(config.a2mba_selftest):
    lit_config.fatal("a2mba-selftest was not built; run the check-a2mba build target")
if not os.path.isfile(config.a2mba_wrapper):
    lit_config.fatal(f"A2MBA wrapper was not found at {config.a2mba_wrapper}")
if not config.a2mba_host_triple:
    lit_config.fatal("the LLVM package did not report a host target triple")

tools = {
    "%FileCheck": require_tool("FileCheck", config.a2mba_filecheck),
    "%clang": require_tool("clang"),
    "%llc": require_tool("llc"),
    "%opt": require_tool("opt"),
}

config.environment["PATH"] = os.pathsep.join(
    [config.llvm_tools_dir, config.environment.get("PATH", "")]
)
config.environment["A2MBA_PLUGIN"] = config.a2mba_plugin
config.environment["A2MBA_CLANG"] = tools["%clang"]

config.substitutions.extend((name, quote(path)) for name, path in tools.items())
config.substitutions.extend(
    [
        ("%a2mba_plugin", quote(config.a2mba_plugin)),
        ("%a2mba_host_triple", config.a2mba_host_triple),
        (
            "%a2mba_opt",
            f"{quote(tools['%opt'])} -load-pass-plugin={quote(config.a2mba_plugin)}",
        ),
        ("%a2mba_selftest", quote(config.a2mba_selftest)),
        (
            "%a2mba_redzone_prefix",
            "IR-STATIC" if config.a2mba_static_llvm_fallback else "IR-DYNAMIC",
        ),
        (
            "%a2mba_wrapper",
            f"{quote(config.python_executable)} {quote(config.a2mba_wrapper)}",
        ),
        ("%python", quote(config.python_executable)),
    ]
)

config.available_features.update(
    {"a2mba-plugin", "a2mba-selftest", "a2mba-wrapper", "clang", "llc"}
)
windows_developer_environment = (
    any(
        config.environment.get(name)
        for name in ("VSCMD_VER", "VCINSTALLDIR", "VCToolsInstallDir")
    )
    or lit.util.which("link", config.environment.get("PATH", "")) is not None
)
if not config.is_cross_compiling and (os.name != "nt" or windows_developer_environment):
    config.available_features.add("host-executable")
if "X86" in config.targets_to_build or "all" in config.targets_to_build:
    config.available_features.add("x86-registered-target")
if os.name == "nt":
    config.available_features.add("system-windows")
else:
    config.available_features.add("system-linux")
