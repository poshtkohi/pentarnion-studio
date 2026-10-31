# Pentarnion Studio

Pentarnion Studio is a cross-platform scientific programming environment for
Pentarnion algebra and numerical experiments. It combines a MATLAB-style command
window, a script editor, native numerical modules and dockable scientific figures.

Project website: [pentarnion.com](https://pentarnion.com)

This is a research preview. Features have explicit mathematical and numerical
limits; the language specifications and examples describe the supported cases.
An implementation of a research method is not evidence of a new scientific result.

## Start here

- [Build Studio on Windows or Linux](docs/BUILDING.md) — prerequisites, pinned
  dependency recipes, command-line scripts and Code::Blocks.
- [Programming guide](documentation/source/fundamentals.md) and
  [quick start](documentation/source/quick-start.md).
- [Examples](examples/README.txt) and [language specifications](specs).
- [Tested platforms and limitations](docs/TESTED_PLATFORMS.md).
- [Third-party licences](THIRD_PARTY_NOTICES.md) and
  [binary redistribution](docs/REDISTRIBUTION.md).
- [Contributing](CONTRIBUTING.md) and [security reporting](SECURITY.md).

## What is included

The repository contains the editor and GUI, parser/type checker/interpreter,
native callback generators, algebra implementations, numerical module interfaces
and implementations, export runtimes, regression tests, synthetic examples,
dependency locks and build recipes. The optional standalone Windows packaging
sources, native bridge and compiled export adapters are in `packaging/`.

Native ODE and other supported numerical callbacks compile to cached DLL/SO
modules. Changing runtime parameter values does not require recompiling an
unchanged callback. Matrix and solver operations share Studio's CPU budget.
Nonassociative multiplication retains operand order and parentheses.

Supported native algebra contexts include real and complex numbers, Pentarnions,
quaternions, octonions, sedenions, dual numbers and dual quaternions, and validated
user-supplied multiplication tables. Consult the specifications for the exact
operations implemented in each context.

## Build entry points

After preparing the dependencies described in the build guide:

```powershell
# Windows; one build worker by default
powershell -ExecutionPolicy Bypass -File tools/build-windows.ps1 -Help
```

```sh
# Linux; one build worker by default
bash tools/build-linux.sh --help
```

These scripts configure and build with CMake/Ninja. They do not silently download
or install system packages. The documented bootstrap step prepares dependencies
separately. The same CMake graph is used by the Code::Blocks project.

## Research papers

Studio builds on the algebra and models described in these preprints by
Alireza Poshtkohi and Susanne Pumplün:

1. [A Five-Dimensional Nonassociative Algebra: elliptic integrability and bracketing-dependent dynamics](https://arxiv.org/abs/2610.34202).
2. [From Quaternions to Pentarnions I: Algebra, geometry, and a spin model](https://arxiv.org/abs/2610.31485).
3. [From Quaternions to Pentarnions II: quadratic differential equations](https://arxiv.org/abs/2610.34201).

See [paper details and citation formats](docs/RESEARCH_PAPERS.md). Cite the relevant
papers when using their scientific results, alongside the exact software version.
The preprints are separate research works; their manuscripts are not distributed
under the software licence merely by being cited here.

## Downloads

Installers, executable files and corresponding-source companion archives belong
in [GitHub Releases](https://github.com/poshtkohi/pentarnion-studio/releases).
They are not committed to source control. Only download a release whose release
notes identify its source revision, checksums and tested platforms.

Signing is not currently approved by SignPath Foundation. Open-source signing
tools alone do not provide a publicly trusted Windows signing identity. See
[signing preparation](docs/SIGNING.md).

## Licence

Pentarnion Studio's first-party code, documentation, examples and reusable
runtime helpers are licensed under the [Apache License 2.0](LICENSE).
Copyright 2026 Alireza Poshtkohi and Susanne Pumplün. See [NOTICE](NOTICE)
for attribution and retained GTG component provenance.

Third-party code, libraries, fonts and tools retain their own licences and
notices; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[redistribution guidance](docs/REDISTRIBUTION.md). Users' independently authored
models, scripts, data and results do not automatically acquire Studio's licence.
