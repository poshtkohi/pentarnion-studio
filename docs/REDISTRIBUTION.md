# Source and binary redistribution

Pentarnion Studio's confirmed-owned first-party implementation, documentation, examples and reusable runtime helpers are licensed under Apache-2.0; see the root `LICENSE` and `NOTICE`. The copyright holders have confirmed authority to release the included material, including the GTG-derived components. Third-party material retains its own terms. This document describes the release process and those boundaries; it does not make a future source-hosting offer.

## Source repository

Keep the implementation, tests, examples, dependency locks, patches and editable build/packaging sources required to rebuild the documented targets. Keep GTG adaptation provenance with the affected GUI/Copilot files. Preserve upstream notices rather than applying the first-party SPDX identifier to vendor files.

Exclude credentials, personal preferences, recovery/checkpoint files, user data, compiler caches, build outputs, proprietary SDKs and downloaded binary dependency trees. Put built distributions in GitHub Releases, rather than committing large executables into Git history. Record the source commit and build configuration for each release.

The distribution implementation must be included if its binaries are released. In particular, include the native bridge and the Python/MATLAB adapter generation/build sources; a declaration-only SDK or prebuilt DLL alone is not the editable implementation. Directory names inherited from an earlier private build are not a reason to omit first-party source that is now authorised for publication.

## Binary release checklist

1. Build from the documented source commit, with pinned dependency inputs and recorded local patches.
2. Inspect the resulting PE/ELF dependency closure. Reuse the verified no-JBIG TIFF configuration when producing the supported release; do not delete a required JBIG DLL from a JBIG-linked TIFF build.
3. Include original notices and exact corresponding-source/configuration material for the shipped copyleft libraries and tools. Provide LGPL replacement/relinking rights without conflicting restrictions on those libraries.
4. For the statically linked pagmo module, publish enough first-party and library source/build information to rebuild the combined module. Retain the matching library source/patches and existing relink companion when redistributing the earlier binary. Validate a replacement/relink route instead of assuming it works.
5. Preserve Eigen/MPL-covered source and per-file notices, along with the other dependency notices. The root software licence cannot remove these terms.
6. Apply allowed resource/metadata changes before signing. Sign only eligible artifacts under the chosen service's rules. Signing changes hashes: refresh SDK/release manifests, package the identical bridge DLL in every required location, and test the actual final installer.
7. Publish hashes, build identity, platform limitations and the matching source/material locations alongside the release. Never present an unexecuted hosted CI job, signature or platform check as passed.

## Generated models and exports

Source files entered by users are not automatically licensed by Pentarnion's application licence. Native model DLLs and Python/MATLAB exports may include or link reusable Studio runtime implementation. The first-party runtime/helpers are covered by Apache-2.0, which does not require independently authored user models to adopt that licence. Dependencies and copied third-party material retain their applicable terms.

Export packaging must provide the complete Apache-2.0 text and applicable Studio `NOTICE` attribution with copied first-party runtime/helpers, for both inline-source and compiled-runtime routes. Use the helper's scoped companion names `PENTARNION-RUNTIME-LICENSE.txt` and `PENTARNION-RUNTIME-NOTICE.txt`; do not label the user's own model as Apache-2.0. Preserve relevant upstream notices for any bundled dependencies. A root repository licence does not by itself put these notice files into an exported bundle: verify the actual generated files before redistributing them.

The existing GUI exporters do **not** automatically add these licence/NOTICE companions. Use `tools/export-with-notices.py` (see its `--help`) to prepare the companions, or copy the complete root `LICENSE` and `NOTICE` to those scoped names beside an existing export. This packaging step adds notices without changing the exported equations or helpers. Retain any separately applicable dependency notices and check the actual output directory before sharing it.

```sh
# Source checkout: invoke the CLI exporter and attach the runtime companions.
python tools/export-with-notices.py export model.p new-python-bundle --target python --exporter path/to/penta-export
# Or attach to an existing GUI/CLI export, including MATLAB and compiled runtimes.
python tools/export-with-notices.py attach existing-export-directory
```

For the standalone Windows installation, the helper is in `tools/` and the
licence originals are in `licenses/Pentarnion/`. Run it using the bundled
`penta-python.exe`, or an existing Python 3.11+ installation. Identical companion
files are retained; different existing notices are never overwritten.

The GCC Runtime Library Exception is conditional on the covered files and eligible compilation. It does not remove the GPL obligations of distributing GCC itself or standalone GCC libraries. Retain the original wheel notices; do not claim the bundled GCC 16.2 source reproduces every library embedded in third-party Python wheels. The preserved wheel provenance describes its evidence and uncertainties.

MATLAB integration uses external proprietary APIs. The first-party adapter and generator sources are Apache-2.0; that grant does not cover MathWorks material. Do not publish `mex.h`, `matrix.h`, MathWorks import libraries, MATLAB installations or licence files. Building and running MATLAB adapters requires the separately licensed external MATLAB installation and compliance with its applicable terms. Audit any additional GPL-covered dependencies separately rather than assuming the first-party licence overrides their linking conditions.

## Platform runtime carve-outs

Keep Microsoft's original Windows Python redistribution conditions and wheel notices with the applicable runtime components. Do not apply Microsoft-specific restrictions to the open-source application, LGPL replacement/debugging rights or user scripts. These runtime components are not claimed to be open source. Optional signing services decide whether a particular platform-runtime arrangement meets their System Libraries exception.

## Existing local installer

The previously tested unsigned local installer has SHA-256 `964ff80610605b1577c79bdafd54c736834aa0f474cf52413bea538fdf7b67ef`. Its 105 installed checks establish that artifact's tested behaviour. It was built from the earlier distribution snapshot, not from a future public source tag. Licence-aware wording, source layout or signing changes require a new identified build and appropriate final-artifact verification. Do not relabel the old hash as a newly licensed or signed release.
