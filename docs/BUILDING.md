# Building Pentarnion Studio

CMake and Ninja own the build graph on Windows and Linux. The checked-in
Code::Blocks projects delegate to the same graph. Configure and build commands
use existing dependencies; they do not download packages or install system tools.
Use an output directory outside the source files.

## A small source check

The algebra-only project compiles the existing eight algebra implementation files
and three existing test suites. It needs C++20, CMake 3.24+, Ninja, Eigen3,
nlohmann-json and Boost headers. It does not build Studio, the interpreter or the
scientific solver modules.

On Linux, after obtaining those development prerequisites:

```sh
bash tools/build-linux.sh --algebra-only --configuration Release --jobs 1 --test
```

On Windows, with an existing UCRT64 compiler and dependency headers:

```powershell
./tools/build-windows.ps1 -AlgebraOnly -Configuration Release -Jobs 1 -Test
```

The standalone equivalent is `cmake -S cmake/algebra-check -B build/algebra -G
Ninja`, followed by `cmake --build build/algebra --parallel 1` and `ctest
--test-dir build/algebra --output-on-failure`. Add `-DCMAKE_PREFIX_PATH=...` when
headers/configuration packages are outside the compiler's normal search paths.

## Windows development build

Use an existing MSYS2 **UCRT64** C/C++ compiler. The default location is
`C:\msys64`; set `PENTA_MSYS2_ROOT` or pass `-MsysRoot` when it is elsewhere.
The build scripts never modify that installation. Do not mix MSYS `/usr` or
MINGW64 libraries with the UCRT64 dependency prefix.

The full build needs the project's pinned Qt6, Eigen, JSON, VTK, JKQTMathText,
SUNDIALS, XLSXIO, OpenBLAS, Boost.Serialization, TBB, Ceres, NLopt, pagmo and MFEM
inputs. Their project-local archives and SHA-256 values are recorded under
`third-party/*.lock.json` and `third-party/mfem-4.10-pin.json`. PETSc is an optional
module selected when its SDK is available; its absence is not full PETSc support.

Prepare dependencies explicitly when needed:

```powershell
$env:PENTA_MSYS2_ROOT='C:\msys64'
$env:PENTA_BUILD_JOBS='1'
./tools/bootstrap-windows.ps1
./tools/build-windows.ps1 -Configuration Release -Jobs 1
```

Preparation downloads only missing pinned inputs, verifies checksums, and writes
the SDK/source/build files under this checkout's `third-party`. It does not install
MSYS2 or change global PATH. Use `./tools/bootstrap-windows.ps1 -Offline` to verify
that **all** required cached archives are present and correct before preparation
starts. A missing or altered input fails that check before extraction/building.
Offline mode is a cache replay, not an installed-prefix integrity audit.

The pinned Boost package includes the selected static Serialization archive,
its five transitive static archives and their configuration metadata. Preparation
and the wrapper select that static configuration explicitly; a separate global
Boost.Serialization installation is not required.

After dependencies are ready, these commands stay offline:

```powershell
./tools/build-windows.ps1 -Configuration Debug -Jobs 1 -Test -Offscreen
./tools/build-windows.ps1 -Configuration Release -Headless -Jobs 1
./tools/build-windows.ps1 -Action Configure -DryRun
```

`-Headless` omits the IDE while retaining the requested configuration. It still
builds the interpreter/scientific graph and requires its numerical dependencies.
It is distinct from `-AlgebraOnly`.

Use `-SourceDirectory`, `-BuildDirectory` and `-DependencyRoot` to reuse a separate
read-only dependency tree. `-Compiler`, `-CMake`, `-Ninja`, `-PrefixPath` and
`-CMakeArguments` provide explicit toolchain/package overrides. For example:

```powershell
./tools/build-windows.ps1 -SourceDirectory C:\work\pentarnion-studio `
  -BuildDirectory C:\work\build\pentarnion-release `
  -DependencyRoot D:\dependencies\pentarnion\third-party -Jobs 1
```

Only `-Run` explicitly opens Studio. Tests run serially; `-TestRegex` selects a
suite and `-TestTimeout` bounds each test (default 300 seconds). Offscreen tests
do not establish interactive desktop rendering or responsiveness.

## Linux development build

The recorded full Linux baseline is Ubuntu 24.04.5, GCC13.3, Eigen3.4 and Qt6.4.2.
The build uses C++20, CMake3.24+, Ninja, a C compiler for dependencies, Python3,
curl, unzip, pkg-config, OpenMP and development packages for Eigen, JSON,
Boost/Serialization, TBB, OpenBLAS/LAPACKE, Expat, Minizip, zlib, bzip2, FreeType,
OpenGL/X11/Xt and Qt6 Core/Gui/Widgets/OpenGLWidgets/SVG/XML/PrintSupport/Network.
Qt Test is also required when IDE tests are enabled. Obtain these through your
distribution or an explicit local prefix; no local script runs apt/sudo for you.

`tools/linux-acceptance-dependencies.sh` explicitly builds the pinned SUNDIALS7.9,
XLSXIO0.2.37, Ceres2.2, NLopt2.11, pagmo2.19.1, MFEM4.10, PETSc3.25.1,
JKQTMathText commit and VTK9.6.2 inputs into project-local prefixes. It can access
the network when an archive is missing. Existing archives are hash-checked and
reused. This longer preparation step is separate from ordinary configure/build.
For a strictly offline build, use the already prepared prefixes and the build
wrapper; the Linux preparation script has no separate offline preflight mode.

```sh
export PENTA_BUILD_JOBS=1
bash tools/linux-acceptance-dependencies.sh
export PKG_CONFIG_PATH="$PWD/third-party/petsc-linux-install/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
bash tools/build-linux.sh --configuration Release --jobs 1 -- -DPENTA_BUILD_PETSC=ON
export LD_LIBRARY_PATH="$PWD/third-party/petsc-linux-install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
bash tools/build-linux.sh --configuration Release --jobs 1 --test --offscreen
```

Repeat `--prefix DIR` for additional package prefixes; use `--compiler FILE`,
`--cmake FILE`, `--ninja FILE`, `--dependency-root DIR` or CMake arguments after
`--` for other layouts. `--headless`, `--algebra-only`, `--test-regex` and
`--dry-run` correspond to the Windows options. The wrapper does not open Studio.
The completed IDE is `build/linux-release/bin/pentarnion-studio`.

## Build parallelism and Code::Blocks

Builds default to one compile worker. `PENTA_BUILD_JOBS`, `-Jobs` or `--jobs`
accepts 1..1024; zero explicitly selects the detected logical CPU count. This is
**build parallelism only**. It does not alter Studio's runtime CPU budget, the
language's thread options, callback caching, or mathematical semantics.

Open `Pentarnion-Windows.cbp` or `Pentarnion-Linux.cbp` and choose Release/Debug.
Their existing `tools/build.ps1` and `tools/build.sh` entry points now delegate to
the wrappers. CMake/Ninja retain dependency decisions and Qt AUTOMOC. Set the
environment overrides before starting Code::Blocks; no global IDE configuration
is changed. The direct CMake presets remain available for the default checkout
layout; the wrappers support relocated toolchains without editing those presets.

## CI and evidence

Automatic CI performs script parsing and the small Linux algebra build. Full
Windows/Linux Studio builds require an explicit `workflow_dispatch` selection;
those jobs install prerequisites only in disposable hosted runners. Action
identities are pinned to commits. The runner/distro compiler packages can change;
CI prints versions, and this is not a promise of bit-identical toolchain replay.
No prepared workflow has been dispatched as part of this source handoff.

The new Windows algebra entry point was built from unchanged copied source with
one BelowNormal compiler worker: all three existing suites passed. The selected
pinned Boost.Serialization closure configured, linked and completed a vector
serialization round trip without global Boost fallback. PowerShell5 help/parse
and Bash syntax/help checks were also exercised. This is bounded build tooling
acceptance, not a fresh full-framework validation.

The separately validated Windows11 installer (31 October2026) passed105 actual
isolated installed checks. Historical full source acceptance (27 October2026)
recorded Windows170/170 and Linux169 passed/one expected OpenBLAS-worker skip.
The current GUI changes have not received a fresh Linux execution. Windows10 VM
acceptance is still pending. See `docs/TESTED_PLATFORMS.md` and release validation records for
the exact revisions, commands and limits; older results do not validate new code.

The ordinary source build uses readable native/export support implementation.
The distribution bridge and compiled Python/MATLAB adapters have separate editable
packaging builders and toolchain prerequisites. MATLAB adapter rebuilding requires
the external licensed MATLAB SDK/runtime; no MATLAB installation is redistributed.
Do not mistake excluded frozen binaries or source archives for missing editable
source: the public packaging source tree and its reconstruction guide describe
those inputs explicitly.
