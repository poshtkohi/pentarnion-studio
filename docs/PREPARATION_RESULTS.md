# Public-source preparation results — 31 October 2026

This document records local preparation checks. The copyright holders have
confirmed Apache-2.0 and authority to release the included first-party code,
documentation and GTG-derived components. The root `LICENSE`, `NOTICE`, README
and citation metadata reflect that decision. Public source publication is
authorised; publication state is recorded by the repository and release history.
No signed release or SignPath Foundation approval is claimed.

## Source and privacy

- A new source tree was curated from the current implementation and a frozen
  set of synthetic examples. Development Git history was not imported. The
  original project, installed dependencies and the user's edited example remain
  untouched.
- Full GUI/compiler/interpreter/numerical/export implementation, tests, examples,
  specifications and dependency/build recipes are included. The optional
  standalone distribution's native bridge and compiled export adapters are also
  supplied as editable sources, with their generators.
- Papers, private research data, credentials, certificate/private-key files,
  compiled binaries, installed SDKs, caches and build trees are excluded.
  Redacted high-confidence credential/private-key pattern scans found no matches.
  Pattern scans supplement review; they do not prove absence of secrets.
- Mathematical source/coefficients are preserved. Public curation changes to
  reference-generator metadata, example comments and manual citations remove
  private manuscript references without changing numerical program statements.
- The previously tested distribution's 16 guarded application adaptations and
  four new support files are included with their original hash-checked contents.
  `PENTA_DISTRIBUTION_BUILD` remains **OFF** by default; the normal inline runtime
  implementations remain readable and available.

## Checks performed for this preparation

| Check | Actual result |
| --- | --- |
| Fresh Windows algebra-only build through the new wrapper | Three existing algebra test suites passed; C++20/GCC 16.2 UCRT64, one worker. This is not a full Studio build. |
| Pinned Boost.Serialization selected-payload proof | Clean configure/link and vector serialisation round trip passed using the pinned static archive/configuration closure, without global Boost fallback. |
| Optional native bridge rebuilt from curated public sources | 48,733 independent reference/parity checks passed: 7,289 base, 4,788 dense and 36,656 automatic-differentiation checks; zero failures, one compiler worker. |
| Integrated full headless CMake configure | Passed using existing dependency SDKs, including detected PETSc. No full target build or GUI launch was performed by this configure check. |
| Build entry points | Windows PowerShell syntax/help and Linux Bash syntax/help checked locally. A Bash syntax check on Windows is not a native Linux build. |
| Public-source audit regression tests | Five checks cover ordinary sources, redacted secret diagnostics, executable/generated-file rejection, licence gating and plain-text `COPYING.LIB` preservation. |

The pinned Boost fix selects six static archives and 56 configuration files from
the already hash-pinned package. Other bootstrap changes bound build workers and
make paths/configurations explicit; they do not change mathematical algorithms
or Studio's runtime threading semantics.

The main platform matrix is in [TESTED_PLATFORMS.md](TESTED_PLATFORMS.md). Hosted
CI, the current Linux GUI, a clean Windows 10 VM, fresh Python/MATLAB adapter
rebuilds and a newly licensed public installer have not been validated by these
preparation checks. No desktop graphics stress test or live Copilot request was
performed.

## Remaining release steps

Binary release work must use the documented no-JBIG TIFF variant and retain the
exact applicable notices, corresponding sources and relinking materials. Verify
Apache-2.0 licence/NOTICE companions for first-party runtime/helpers in actual
generated exports. Regenerate and review public DOCX/PDF manuals from the
sanitized editable text before including them in a new installer; old
private-reference versions are not in this repository. Build and verify a newly
identified installer rather than relabelling the earlier unsigned artifact.

See [PUBLIC_RELEASE.md](PUBLIC_RELEASE.md), [REDISTRIBUTION.md](REDISTRIBUTION.md)
and [SIGNING.md](SIGNING.md). The confirmed first-party rights and licence do not
replace dependency compliance, signing-service eligibility checks or Windows
reputation requirements.
