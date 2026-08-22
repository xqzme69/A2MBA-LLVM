# Installation

## Requirements

- CMake 3.24 or newer.
- LLVM and Clang 21.x development files. Other LLVM majors are rejected at configure time and by the wrapper.
- A C++20 compiler compatible with the selected LLVM build.
- Python 3.8 or newer for `a2mba-clang`, validation helpers, and tests. Test builds also need the pinned package from `requirements-test.txt`.
- An x86-64 Linux or Windows target for architectural transforms.

LLVM's C++ ABI and plugin APIs are version-sensitive. Build the plugin against the Clang/LLVM installation that will load it. Matching `.so` or `.dll` extensions prove nothing; `--doctor` performs an actual load test.

## Source build

Locate the LLVM CMake package directory. On installations that provide `llvm-config-21`:

```bash
llvm-config-21 --cmakedir
```

Configure and build:

```bash
python -m pip install -r requirements-test.txt
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/path/to/llvm-21/lib/cmake/llvm
cmake --build build --config Release
```

On Linux the plugin is normally under `build/lib/A2MBA.so`. On a multi-configuration Windows generator it is normally under `build/bin/Release/A2MBA.dll`.

PowerShell example:

```powershell
cmake -S . -B build `
  -DLLVM_DIR='C:\LLVM-21\lib\cmake\llvm'
cmake --build build --config Release
```

### Windows portable LLVM 21.1.8

The official portable Windows LLVM 21.1.8 package sets `LLVM_ENABLE_PLUGINS=OFF`. CMake detects this and builds `A2MBA.dll` as a compatibility module linked to the required LLVM components. Overriding that package setting is unsupported.

The compatibility DLL stays pinned until the loading Clang or `opt` process exits. Pass-created LLVM objects can outlive the loader's plugin handle, and unloading early would leave dangling code and data. Function analyses are also registered and queried inside the DLL, keeping objects and `AnalysisKey` identities in the same statically linked LLVM copy.

That boundary also changes how AAMBA protects the SysV red zone when the Windows-hosted DLL processes Linux-target IR:

```text
leaq -128(%rsp), %rsp
pushfq
... state-sensitive primitive ...
popfq
leaq 128(%rsp), %rsp
```

The stack adjustment and flag sandbox form one inline-assembly unit. `LEA` leaves flags untouched; `pushfq` and `popfq` save and restore them. This avoids passing an LLVM `AttributeList` across the host/DLL boundary. A normal plugin-enabled LLVM build adds `noredzone` to the affected Linux-target function instead. The flags contract is the same on both paths. Windows targets do not use the SysV red zone.

Some copies of the portable package retain the builder's missing absolute path to `diaguids.lib`. Install the Visual Studio DIA SDK or configure `A2MBA_DIAGUIDS_LIBRARY` with the exact x64 `diaguids.lib` path if CMake reports that error.

## Tests

Tests are enabled by default and use LLVM's lit/FileCheck tools through CTest:

```bash
python -m pip install -r requirements-test.txt
cmake --build build --config Release --target check-a2mba
ctest --test-dir build -C Release --output-on-failure
```

Install `requirements-test.txt` with the same Python interpreter that CMake selects. If several interpreters are installed, set `Python3_EXECUTABLE` explicitly. An existing standalone lit runner can be supplied with `LLVM_EXTERNAL_LIT` instead.

The test scaffold takes `clang`, `opt`, `llc`, and normally `FileCheck` from the selected LLVM 21 package's `LLVM_TOOLS_BINARY_DIR`. All four tools must come from the same release. If only `FileCheck` lives elsewhere, pass its exact path:

```bash
cmake -S . -B build \
  -DLLVM_DIR=/path/to/llvm-21/lib/cmake/llvm \
  -DA2MBA_FILECHECK_EXECUTABLE=/path/to/llvm-21/bin/FileCheck
```

This changes FileCheck discovery only. `clang`, `opt`, and `llc` still come from the selected LLVM package.

The `check-a2mba` target builds the plugin and the native `a2mba-selftest` before running lit. To omit test dependencies from a packaging build, configure with `-DA2MBA_BUILD_TESTS=OFF`.

For a Debug/nightly plugin build with host compiler sanitizers:

```bash
cmake -S . -B build-sanitize \
  -DLLVM_DIR=/path/to/llvm-21/lib/cmake/llvm \
  -DA2MBA_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
```

## Install

```bash
cmake --install build --config Release --prefix /desired/prefix
```

The install layout is:

```text
bin/a2mba-clang           # Linux
bin/a2mba-clang.py        # Windows
include/a2mba.h
lib/a2mba/A2MBA.so        # Linux
lib/a2mba/A2MBA.dll       # Windows layout may use the configured libdir
share/doc/a2mba/           # README, citation metadata, and documentation
share/licenses/a2mba/      # A2MBA, LLVM, and linked third-party notices
```

The installed wrapper searches the companion `lib/a2mba` directory. Custom layouts should pass `--plugin` or set `A2MBA_PLUGIN`. On Windows it also needs the exact matching `opt`, found beside Clang by default or selected with `--opt`/`A2MBA_OPT`.

## Prebuilt plugins

For a prebuilt plugin, match the exact LLVM release, operating system, and architecture reported by Clang. Build from source when that combination is unavailable. A shared major version is not enough. An LLVM 21.1 plugin and an LLVM 21.0 loader are compatible only when the release explicitly guarantees that pairing.

Run the smoke check before use:

```bash
a2mba-clang --doctor --clang /path/to/clang-21 --plugin /path/to/A2MBA.so
```

Equivalent PowerShell:

```powershell
python C:\A2MBA\bin\a2mba-clang.py --doctor `
  --clang C:\LLVM-21\bin\clang.exe `
  --plugin C:\A2MBA\lib\a2mba\A2MBA.dll
```

## Compile through Clang

```bash
a2mba-clang --level balanced source.c -O3 -o app
```

Wrapper options may appear among normal compiler arguments. Unrecognized arguments keep their original order. The wrapper sets `A2MBA_OPTIONS`; on the direct path it also adds `-fpass-plugin=<path>`.

That direct path is used on Linux and for LLVM-output-only commands. Optimized Windows object, assembly, and executable builds use three processes instead: Clang emits optimized bitcode, `opt -passes=a2mba` writes protected bitcode, and Clang lowers or links that serialized module with further LLVM passes disabled. This is required by the official package's static-LLVM compatibility DLL; loading the DLL and running the backend in one process can mix LLVM objects from two static copies. Input files must be visible on the command line rather than hidden inside a response file; flag-only response files are accepted. Compile-only commands take one translation unit at a time, as normal build systems already do.

Clang's automatic extension runs at `-O1` or higher and is skipped at `-O0`. The documented production path uses `-O3`, matching the paper's placement after standard optimization. The staged Windows path enforces the same rule.

The source SDK is optional when using `functions=all` or `functions=regex:...`. For the default annotation mode, add the installed include directory and include `<a2mba.h>`.

## Run through opt

Linux:

```bash
export A2MBA_OPTIONS='mode=verified;level=balanced;seed=1;functions=all'
opt -load-pass-plugin=build/lib/A2MBA.so \
  -passes=a2mba input.ll -S -o protected.ll
```

Windows PowerShell:

```powershell
$env:A2MBA_OPTIONS = 'mode=verified;level=balanced;seed=1;functions=all'
opt -load-pass-plugin=build\bin\Release\A2MBA.dll `
  -passes=a2mba input.ll -S -o protected.ll
```

IR passed directly to `opt` needs an x86-64 Linux or Windows `target triple`; Clang-emitted IR normally has one. A missing or unsupported triple leaves the module unchanged.

## Troubleshooting

### Unsupported LLVM major

Both CMake and the wrapper require LLVM major 21. Point `LLVM_DIR` and `--clang` at the same installation. There are no compatibility branches for LLVM 15-20 or 22+.

### Plugin found but load fails

Use an absolute `--plugin` path and run `--doctor`. Common causes are a different LLVM build, a Debug/Release runtime mismatch on Windows, missing LLVM shared libraries, or loading a plugin for the wrong architecture.

After a successful load, the portable Windows compatibility DLL remains resident until that compiler or `opt` process exits. The produced application neither installs nor retains it.

### Wrapper cannot find the plugin

Use `--plugin PATH` or `A2MBA_PLUGIN`. In a source checkout it searches common `build/bin`, `build/lib`, and configuration subdirectories. In an installation it searches `lib/a2mba`.

### Function was not transformed

The default is `functions=annotated`. Check that the annotation survived normal compilation. Use `A2MBA_PROTECT_NOINLINE` when inlining removes the boundary, or `--functions all` for an experiment. A selected function can still remain unchanged because of poison flags, unsupported widths or targets, probability, or existing user inline assembly.

### LTO

ThinLTO and Full LTO are not supported in v0.1. With LTO enabled, the plugin has no validated final optimizer position.
