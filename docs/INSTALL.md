# Installation

## Requirements

- CMake 3.24 or newer.
- LLVM and Clang 21.x development files. Other LLVM majors are rejected at configure time and by the wrapper.
- A C++20 compiler compatible with the selected LLVM build.
- Python 3.9 or newer for rule generation, `a2mba-clang`, validation helpers, and tests.
- Z3 for build-time verification of the hybrid rule table. The pinned `z3-solver` package satisfies this requirement.
- An x86-64 Linux or Windows target for architectural transforms.

Use the same Clang/LLVM installation to build and run the pass. On Linux it loads the plugin; on Windows it runs `a2mba-opt.exe`. The matching `21` in two filenames is not enough to establish compatibility. `--doctor` checks by running the pass and generating an object.

## Source build

Find LLVM's CMake package directory. If `llvm-config-21` is available:

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

The Linux plugin normally lands at `build/lib/A2MBA.so`. On Windows, including with multi-config generators, the driver lands at `build/bin/a2mba-opt.exe`.

PowerShell example:

```powershell
cmake -S . -B build `
  -DLLVM_DIR='C:\LLVM-21\lib\cmake\llvm'
cmake --build build --config Release
```

### Windows portable LLVM 21.1.8

The official portable Windows LLVM 21.1.8 package sets `LLVM_ENABLE_PLUGINS=OFF`. CMake builds `a2mba-opt.exe` with the pass linked into the `opt` driver. Do not override that setting or load a DLL linked against another static LLVM copy.

This avoids a concrete failure: LLVM assertion builds salt `DenseMap` hashing with an address in their own LLVM image. Two static copies in one process can then use different hash seeds for the same `LLVMContext`. Small modules may pass before a rehash exposes the mismatch. The standalone driver keeps everything in one LLVM image.

On SysV x86-64, a gadget that saves flags on the stack marks its function `noredzone`. Flag-clobbering variants do not use the stack. Windows has no SysV red zone.

If CMake reports a missing `diaguids.lib`, install the Visual Studio DIA SDK or set `A2MBA_DIAGUIDS_LIBRARY` to the x64 library. Some portable packages retain an unusable absolute path from the machine that built them.

## Tests

Tests are on by default. CTest runs them through LLVM's lit and FileCheck:

```bash
python -m pip install -r requirements-test.txt
cmake --build build --config Release --target check-a2mba
ctest --test-dir build -C Release --output-on-failure
```

Install `requirements-test.txt` into the Python interpreter CMake selects. If it picks the wrong one, set `Python3_EXECUTABLE`. To use an existing lit runner, set `LLVM_EXTERNAL_LIT`.

On Windows, use an x64 Visual Studio developer shell so Clang can link the runtime fixtures. Without a linker on `PATH`, lit marks those tests unsupported. CI enters the developer shell before running them.

The tests take `clang`, `opt`, `llc`, and normally `FileCheck` from the selected LLVM 21 package's `LLVM_TOOLS_BINARY_DIR`. If only `FileCheck` is elsewhere, give CMake its exact path:

```bash
cmake -S . -B build \
  -DLLVM_DIR=/path/to/llvm-21/lib/cmake/llvm \
  -DA2MBA_FILECHECK_EXECUTABLE=/path/to/llvm-21/bin/FileCheck
```

This overrides FileCheck discovery only; `clang`, `opt`, and `llc` still come from the selected LLVM package.

`check-a2mba` builds the platform pass entry point and both native self-tests, checks that an invalid hybrid rule is rejected, runs the nonlinear/stateful models, then runs lit. Packaging builds can omit test targets with `-DA2MBA_BUILD_TESTS=OFF`. Z3 is still needed to verify the hybrid rule table before C++ generation.

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

Installed files:

```text
bin/a2mba-clang           # Linux
bin/a2mba-clang.py        # Windows
bin/a2mba-opt.exe         # Windows standalone pass driver
include/a2mba.h
lib/a2mba/A2MBA.so        # Linux
share/doc/a2mba/           # README, citation metadata, and documentation
share/licenses/a2mba/      # A2MBA, LLVM, and linked third-party notices
```

On Linux, the wrapper looks in the installed `lib/a2mba` directory. Use `--plugin` or `A2MBA_PLUGIN` for another layout. On Windows it looks beside the wrapper and in source-build output directories; use `--opt` or `A2MBA_OPT` to point elsewhere.

## Prebuilt binaries

Prebuilt plugins and drivers must match Clang's LLVM release, operating system, architecture, and runtime. If no binary matches, build from source. Sharing major version 21 is not sufficient.

Check the binary before using it:

```bash
a2mba-clang --doctor --clang /path/to/clang-21 --plugin /path/to/A2MBA.so
```

Equivalent PowerShell:

```powershell
python C:\A2MBA\bin\a2mba-clang.py --doctor `
  --clang C:\LLVM-21\bin\clang.exe `
  --opt C:\A2MBA\bin\a2mba-opt.exe
```

## Compile through Clang

```bash
a2mba-clang --level balanced source.c -O3 -o app
```

For C++ linking, pass `--clang clang++` so the [C++ runtime libraries](https://clang.llvm.org/docs/Toolchain.html#runtime-libraries) are linked. The wrapper preserves that compiler name, including symlinks.

Use the same Windows CRT in a DLL and its C++ host. The DLL regression passes `-fms-runtime-lib=dll -Wl,/nodefaultlib:libcmt` to both builds. Without the second flag, the GNU-style driver can add the static CRT at link time despite the frontend's DLL setting. The wrapper forwards these flags; it does not pick a CRT.

Wrapper options can sit among Clang arguments. An option value stays with its option: `-I --seed` names an include directory, not the wrapper's seed. Other arguments keep their order. The wrapper sets `A2MBA_OPTIONS` and, on Linux, adds `-fpass-plugin=<path>`.

Linux loads the plugin into Clang. Optimized Windows object, assembly, and executable builds use three processes: Clang writes optimized bitcode, `a2mba-opt -passes=a2mba` protects it, and Clang lowers or links the serialized result with further LLVM passes disabled. LLVM-output builds stop after the driver. Before inspecting source paths and options, the Windows wrapper expands response files using LLVM's parser and Clang's quoting rules. It supports nested files, UTF-8/UTF-16, and `--rsp-quoting=windows`. Compile-only commands take one translation unit; link commands can take several source files.

With `-working-directory=<path>`, source lookup and relative LLVM output paths use that directory. `-o -` still writes LLVM output to stdout. Windows regression tests cover paths with spaces and Unicode, extensionless sources selected by `-x`, dependency files, and a two-source link.

Clang runs the automatic extension at `-O1` and above, not `-O0`. The normal production command uses `-O3`, with A²MBA after standard optimization; staged Windows builds follow the same rule.

`functions=all` and `functions=regex:...` do not need the source SDK. With the default `annotated` selection, add the installed include directory and include `<a2mba.h>`.

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
build\bin\a2mba-opt.exe `
  -passes=a2mba input.ll -S -o protected.ll
```

IR passed directly to `opt` needs an x86-64 Linux or Windows `target triple`. Clang normally emits one; without a supported triple, the pass leaves the module unchanged.

## Troubleshooting

### Unsupported LLVM major

CMake and the wrapper require LLVM 21. Point `LLVM_DIR` and `--clang` at the same installation. LLVM 15–20 and 22+ are not supported.

### Plugin or driver check fails

Run `--doctor` with an absolute `--plugin` path on Linux or an absolute `--opt` path to `a2mba-opt.exe` on Windows. Check the LLVM build, Debug/Release runtime, shared libraries, and architecture.

### Wrapper cannot find the plugin

Pass `--plugin PATH` or set `A2MBA_PLUGIN`. The wrapper searches common `build/bin`, `build/lib`, and configuration directories in a source checkout, or `lib/a2mba` after installation.

### Wrapper cannot find the Windows driver

Pass `--opt PATH` or set `A2MBA_OPT`, pointing to `a2mba-opt.exe`, not stock `opt.exe`. Stock `opt.exe` has no `a2mba` pass unless it loads a plugin.

### Function was not transformed

The default is `functions=annotated`. Check whether the annotation survived optimization. If inlining erased the function boundary, use `A2MBA_PROTECT_NOINLINE`; for an experiment, try `--functions all`. A selected function can still be skipped because of poison flags, unsupported widths or targets, selection probability, or existing user inline assembly.

If hybrid search cannot fit within the graph, depth, instruction, or register limits, it leaves the operation unchanged. With diagnostics enabled, the reason is `hybrid planning failure`.

### LTO

ThinLTO and Full LTO are not supported in v0.1; the pass has no validated final position in those pipelines.
