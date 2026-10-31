# Standalone Windows packaging sources

The main Studio build does **not** require the optional compiled distribution
SDK, Cython or MATLAB. Use `docs/BUILDING.md` for an ordinary source build.

This directory preserves the complete editable implementation of the standalone
preview's launchers, installer lifecycle, payload staging, native bridge and
compiled Python/MATLAB export adapters. No first-party algorithm is supplied only
as an opaque binary. The original inline native and export runtimes also remain
readable in `src/ode`, `src/native_functions` and `src/export`.

`PENTA_DISTRIBUTION_BUILD=OFF` is the normal default. The optional `ON` path uses
the same numerical implementations through a compiled SDK and app-local paths.
It is a Windows packaging mode, not a prerequisite for native model compilation.

## Rebuild the optional SDK

Requirements: Python 3.11+ for orchestration; pinned GCC 16.2 UCRT64 and the full
project dependency SDKs. Optional Python adapter compilation uses the CPython
3.12.14 developer installation (headers and `python3.lib`) and Cython 3.1.8.
The MATLAB adapter uses an external, separately licensed MATLAB R2020a developer
SDK. MATLAB and its headers/import libraries are not redistributed.

Use a regular developer Python installation for packaging. The installed
application's isolated embedded Python is intended for running exports and the
standalone notice helper, rather than importing the packaging build modules.

Download the Cython wheel from the official PyPI Cython 3.1.8 release and verify
it against `components/export-python/cython-wheel.json`. No wheel is committed.
Choose a new output directory **outside** the checkout:

```powershell
python packaging/build-components.py prepare --out C:/penta-build/components
python packaging/build-components.py bridge --out C:/penta-build/components --compiler C:/msys64/ucrt64/bin/g++.exe
python packaging/build-components.py python --out C:/penta-build/components --compiler C:/msys64/ucrt64/bin/g++.exe --python-dev C:/Python312/python.exe --cython-wheel C:/downloads/cython-3.1.8-cp312-cp312-win_amd64.whl
python packaging/build-components.py matlab --out C:/penta-build/components --compiler C:/msys64/ucrt64/bin/g++.exe --matlab-sdk "C:/Program Files/MATLAB/R2020a"
python packaging/build-components.py sdk --out C:/penta-build/components
```

The bridge command builds and runs fresh numerical parity executables, one
compiler process at a time. The other commands compile adapters without starting
MATLAB or a desktop graphics session. For a complete distribution SDK both
optional export components are currently required; an ordinary Studio build
needs neither. New component builds require their own acceptance validation.

Configure the application with `-DPENTA_DISTRIBUTION_BUILD=ON`, then build using
the same dependency settings as the normal Windows build. Supply the generated
SDK through the launcher's app-local runtime layout. The runtime SDK manifest
records the actual component hashes. Do not reuse a manifest from another build.

## Build the installer

The first public research preview uses `repackage_public.py` to retain the exact
previously validated Windows numerical executable/SDK bytes while replacing
licences, public documentation, source, examples and package metadata. This is
transparent repackaging, not a claim that hosted CI produced a fresh host build.
Its base input is the immutable validated release directory containing `payload/`,
its original installer and the matching validation/audit receipts.

Use a **clean extraction of the exact release source ZIP** for `--source`; the
script checks both file inventory and byte identities against that ZIP. Choose
a new output directory outside both the source and validated base directories.
Use short Windows staging paths so third-party developer SDK filenames remain
within filesystem/tool limits; no global long-path policy change is needed:

```powershell
python packaging/repackage_public.py --base-release C:/validated-preview --source C:/extracted-release-source --source-archive C:/downloads/Pentarnion-Studio-Source.zip --source-revision COMMIT_SHA --out C:/new-public-release --inno C:/tools/innosetup/ISCC.exe
```

`build_public_manuals.py` creates the local HTML/Markdown guides from the public
authoring text. All styling and formula text are offline. Earlier private-reference
DOCX/PDF manuals are excluded. The new package retains matching third-party
corresponding sources/relink materials and records its own hashes and acceptance.
After a GUI export, use the installed notice helper before redistributing runtime
files; see `docs/REDISTRIBUTION.md`.

For a **fresh host/SDK rebuild**, use the ordinary source and component builders
above, then the original lower-level staging path described below. That path
expects separately generated, reviewed DOCX/PDF manuals under its documented
names; those outputs are not supplied by the offline HTML builder.

`build_release.py --help` lists every explicit input. Prepare a new source archive
using `tools/source-archive.py`, the built application `bin`, deployed vendor DLLs,
the generated runtime SDK, the full pinned MSYS2 development prefix including
package metadata, documentation and a Python runtime built using
`bootstrap_python.py --lock python-dependencies.lock.json --out NEW_DIRECTORY`.
Use Inno Setup 7.1.1 and a new output directory. The builder downloads nothing.
Large dependency corresponding-source/relink companions are separate inputs and
GitHub Release assets. Do not commit their archives or installed SDKs to Git.

The packaging implementation preserves the preview's conservative payload and
relink role checks. A public release must update its licence text and use the
newly confirmed first-party licence. The builder refuses an unlicensed source
root. See `docs/PUBLIC_RELEASE.md` and `docs/REDISTRIBUTION.md` before release.
The old research-preview executable is not silently relicensed by publishing
these sources.

`vendor/libtiff-no-jbig` records the exact no-JBIG rebuild configuration/patch.
Get the hash-pinned upstream tarball separately. Excluding JBIG means the old
development runtime DLL directory must not be copied blindly into a public
installer. All codec/source/notice obligations for the actual closure remain.

Source recipes are reproducible procedures with explicit versions and hashes;
byte-identical output across different machines/toolchains is not claimed.
Do not run desktop graphics stress tests as part of packaging validation.
