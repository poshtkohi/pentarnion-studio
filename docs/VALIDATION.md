# Validation and scope

This source tree contains regression tests, numerical reference fixtures and
educational examples. The algebra and language contracts under `specs/` define
their intended semantics and documented bounds. No private development handoff,
personal build history, unpublished manuscript or local machine QA archive is
included in this public source curation.

Descriptions of historical checks in the feature contracts are not a current
CI result. Record the exact commit, compiler, dependency locks, platform, solver
settings and test commands for each new build. Test only the relevant bounded
workloads when iterating; list skips and untested platforms rather than inferring
success. Desktop graphics stress tests are not required for routine verification.

The three CSV files in `tests/ode/reference/` are generated numerical reference
trajectories, not measurements from people or an experimental dataset. Their
generator records its NumPy/SciPy method, tolerances and hashes. The tiny workbook
and CSV examples demonstrate interchange using explicitly supplied synthetic
values. Preserve full coefficients and operation order when comparing backends.

Solver convergence flags, finite-time chaos metrics and successful fits have
specific numerical meanings. They do not establish a global theorem, parameter
uniqueness or a generally valid biological/physical mechanism. Use independent
checks appropriate to the scientific conclusion.

Current bounded checks: [preparation results](PREPARATION_RESULTS.md).
Platform claims: [tested platforms](TESTED_PLATFORMS.md).
