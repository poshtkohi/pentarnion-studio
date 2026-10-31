# Tested platforms

Keep preparation checks, historical development checks and new release validation
separate. A successful configure is not a successful build; a headless numerical
test is not a desktop interaction test.

| Platform | Evidence | Limits |
| --- | --- | --- |
| Windows 11 x64, host build 26300, GCC 16.2 UCRT64 | The 31 October 2026 standalone preview installer passed 43 installed lifecycle checks and 62 installed workflow checks (105 total). Workflows included native ODE, roots/optimisation/derivatives, algebra arrays, real FD/FEM PDEs, matrix operations, CSV/XLSX and image export. | Evidence belongs to that frozen preview build; it does not automatically validate a new source build. No desktop resize/maximise stress testing. |
| Windows 10 x64, version 1903/build 18362 or later | Intended installer target. | A clean Windows 10 VM run is pending. |
| Ubuntu 24.04.5, GCC 13.3, Qt 6.4.2 | An earlier development snapshot recorded 169 passing tests and one skipped test. | Historical result only. The current GUI and new public packaging have not been validated on Linux. |
| macOS, Windows ARM, other Linux distributions | No current release validation recorded. | Not advertised as tested. |

The current public-source preparation results are recorded separately in
`docs/PREPARATION_RESULTS.md`. CI configuration is supplied, but a workflow file
is not evidence that hosted CI has run. Optional MATLAB/MEX validation requires a
separately licensed external MATLAB installation; MATLAB is not bundled.

The standalone preview was unsigned. It has no SignPath Foundation approval.
CLI and GUI equation typography can differ. File-symlink adversarial testing was
not available on the preparation host; directory-junction tests were performed.

For release validation use one build/test worker initially and keep GUI checks
bounded and offscreen. Do not run continuous window resizing, maximise loops or
graphics stress tests on a contributor's desktop.
