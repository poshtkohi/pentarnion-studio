# Third-party notices

The repository's confirmed first-party licence is Apache-2.0; see `LICENSE` and `NOTICE`. It applies to the included first-party code and other material the copyright holders have confirmed authority to release, including GTG-derived components. It does not replace the licences below. Original copyright, licence, exception and attribution notices remain applicable. Copies are retained in `LICENSES/`; exact versions, source URLs, patches and hashes are recorded by the dependency lock files and release provenance.

## Main components

| Component / inspected version | Role and retained terms |
| --- | --- |
| Qt Base and SVG 6.12.0 | GUI, networking, printing and SVG. Used under applicable LGPLv3 terms; Qt contains additional separately licensed material. Dynamic replacement must remain possible. Consult the actual module notices, not a blanket “all Qt is LGPL” claim. |
| JKQTCommon / JKQTMathText 5.0.0, commit `9e9f0af8b140192e3de7ff866a0013e7da5e1e93` | Math typography, LGPL-2.1-or-later; bundled XITS fonts retain OFL-1.1. |
| VTK 9.6.2 | Scientific views, BSD-3-Clause and retained upstream notices; its transitive image/data dependencies have their own terms. |
| TIFF 4.7.2 no-JBIG variant | Permissive TIFF notices retained. The release variant deliberately disables JBIG; its source/configuration/build record accompanies that binary. A developer's system TIFF may have a different closure. |
| Eigen 5.0.1 | Linear algebra headers, primarily MPL-2.0 with other notices for individual files. Preserve covered-source availability and notices. |
| SUNDIALS 7.9.0: CVODE, CVODES, ARKODE, KINSOL and IDAS | Numerical integration and nonlinear solves; BSD-3-Clause and original notices. |
| Ceres 2.2.0; MFEM 4.10; PETSc 3.25.1 | Numerical backends; retain their respective BSD notices and any separately licensed dependencies. |
| pagmo 2.19.1 | LGPL-3.0-or-later / GPL-3.0-or-later upstream alternatives. The selected library route retains LGPL terms. Static linkage in the evolution module requires corresponding library source, patches and a way to recombine with a modified library. |
| NLopt 2.11.0 | Retain its combined-package notices. The inspected default library includes Luksan routines under LGPL-2.1-or-later; an MIT-only build is a different configuration and is not assumed here. |
| OpenBLAS 0.3.34; LAPACK-related components | Retain the actual BSD and bundled-runtime notices for the selected binaries. |
| Boost 1.92.0; oneTBB 2023.1.0 | Boost Software License 1.0 and Apache-2.0 respectively. |
| XLSX I/O 0.2.37, Expat, minizip/zlib | Spreadsheet/data I/O; retain MIT/zlib and individual dependency notices, including local UTF-8/style patches. |
| nlohmann/json, ExprTk and supporting parsers | Their individual MIT or other retained upstream terms; the project licence does not replace them. |
| GCC 16.2, GNU binutils and MinGW runtime | Separate toolchain components with GPL and applicable runtime exceptions/per-file terms. Shipping the compiler requires its own source/notice treatment; merely compiling independent user code does not automatically impose the compiler's GPL on it. |
| CMake, Ninja and runtime dependency closure | Retain their own licences and the licences of linked libraries. The release closure includes LGPL dependencies beyond the GUI, including CMake/curl's IDN/unistring dependencies. |
| CPython 3.14.10, Tcl/Tk, NumPy, SciPy, Matplotlib and wheel dependencies | PSF, Tcl/Tk and each package's original notices, including bundled fonts/native libraries. Preserve the complete wheel licence directories. Cython's original licence/COPYING notices are retained for the native export build; generated output is addressed by Cython's own published exception. |

The detailed release inventory controls which binaries are actually shipped. A retained package notice is not proof that every tool or optional codec from that package is included. For example, retaining an upstream JBIG notice does not mean the no-JBIG release contains its DLL.

## Microsoft runtime and MATLAB boundaries

The inspected Windows Python/wheel bundle includes `vcruntime140.dll`, `vcruntime140_1.dll` and a wheel-named `msvcp140` DLL. These are Microsoft components, not first-party open-source code. Preserve the full Python Windows additional conditions in `LICENSES/python/LICENSE.txt` and the relevant wheel notices. Those conditions apply to Microsoft Distributable Code and must not be extended into restrictions on Python, Pentarnion code or users' own programs. Do not replace Microsoft's notices or describe its files as relicensed by this repository.

MATLAB is an optional, externally licensed target. MATLAB itself, proprietary SDK headers and MathWorks import/runtime libraries are not supplied by this source repository. Any locally compiled MEX adapter is subject to the selected first-party licensing policy and applicable external MATLAB terms. See `docs/REDISTRIBUTION.md` before distributing such binaries.

## Source and relinking material for binary releases

Binary releases must identify the exact source commit and dependency inputs. Preserve the matching source/configuration/patch collections and source indexes for Qt, JKQTMathText, GNU tools, shipped LGPL libraries and the no-JBIG TIFF variant. Keep pagmo's library source/build inputs and the applicable relink mechanism. Do not substitute an upstream homepage for the distributor-controlled corresponding-source arrangement.

The earlier local installer included explicit corresponding-source and object-relink collections. A new public build must regenerate its inventory and notices from its actual inputs; that earlier installer is not automatically the binary for a newly licensed source tag.

Useful upstream references: [Qt licensing](https://doc.qt.io/qt-6/licensing.html), [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations), [pagmo](https://github.com/esa/pagmo2), [MPL FAQ](https://www.mozilla.org/en-US/MPL/2.0/FAQ/), [GCC runtime exception FAQ](https://www.gnu.org/licenses/gcc-exception-3.1-faq.html.en).
