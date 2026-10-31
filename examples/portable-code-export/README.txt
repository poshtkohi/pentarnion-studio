PORTABLE CHECKED CODE EXPORT

These scripts run in Studio and exercise the documented portable subset.
From the release command line, choose a new output directory:

  penta-export --target python algebra-and-shapes.p python-algebra
  penta-export --target matlab algebra-and-shapes.p matlab-algebra
  penta-export --target python --python-bdf-adapter decay-ode.p python-decay
  penta-export --target matlab --matlab-ode15s-adapter decay-ode.p matlab-decay

Run Python's exported.py with NumPy installed. Figure exports additionally use
Matplotlib; the explicit ODE adapter uses SciPy. Run MATLAB's exported.m with
its directory on the path. Each bundle contains its small target runtime,
source-map.json and compatibility.json. The CLI never overwrites a directory.

The exporter uses Studio's checked typed AST, retaining explicit grouping,
the five ordered coefficients, algebra gamma, matrix vs entrywise products,
one-based column-major indexing, end, masks, reshape and row/column shapes.
Resolved imports of supported named functions are bundled with source maps.
Arithmetic, scalar user functions, ordinary control flow, dense arrays,
display, vector/time plots and basic figure/subplot labels are covered.

The Python BDF adapter requires explicit opt-in. It maps CVODE BDF to SciPy BDF
and generates an analytic real-coordinate Jacobian by forward-mode automatic
differentiation of the emitted RHS. The adaptive steps are independent; this
is not a shared Studio kernel or a promise of identical long trajectories.
Supported ODE settings are t0,t1,sample_step,method="bdf",jacobian="analytic",
rtol and atol. MATLAB's separate explicit adapter uses ode15s with BDF on and
bundles its own forward AD class, without a Symbolic Toolbox dependency. BDF
events, dense queries and simultaneous analytic sensitivities now have explicit
adapters; unsupported methods or options still fail with source diagnostics.

structures-and-ordered-equations.p adds nested records, tuples/destructuring,
named/default function calls, captured expression lambdas and complex scalars.
It also exercises real linsolve/inv/det and complete ordered Pentarnion equation
operators. Real coefficient operators preserve nonassociative ordering; a single
scaled SVD is reused for reliability and solving. Generic imported algebra/table
and native fixed-array state export remain separate work.

Scientific examples use the separate --scientific-adapters flag. Fitting maps
Ceres LM/dogleg to documented SciPy/MATLAB least-squares algorithms; minimization
maps a bounded real objective/constraint subset with explicit target stopping
policies. weighted-graph-analysis.p covers complete edge IDs/loops/components,
negative-cycle status and centralities. diffusion-and-field-decay.p uses real
and five-coefficient1D FD/P1 FEM fields, RK4/IMEX and coefficient extraction.
newton-motion-and-sampled-geometry.p shows RK4/Verlet and sampled curve/field
plots. MATLAB fitting/fminunc/fmincon need Optimization Toolbox; the included
PDE, graph, Newton and geometry algorithms do not require that toolbox.

events-mass-and-analytic-mechanics.p requires both --scientific-adapters and
the target BDF flag. It covers a localized terminal root, integrated parameter/
initial sensitivities, regular state/time mass and generated L/H mechanics.
hybrid-resets-and-reverse-costs.p adds complete reset sides, saltation and an
actual continuous reverse cost adjoint with running/pre-reset event costs and
a moving terminal objective. Reverse integration uses the target's adaptive
forward dense information, independently of the requested plot sample grid.
Target BDF algorithms, telemetry and stopping policies differ from SUNDIALS;
none of these mappings reuses Studio DLLs or promises identical chaotic paths.

Unsupported syntax/calls produce X1601 at the original byte span and no
executable bundle. Native kernel/cache/MEX export, generic-algebra/fixed-array
unknown ABI adapters, intrinsic geometry, adaptive/multidimensional PDE,
constraint/generalized-force/unit-aware mechanics, singular/general DAE or
non-BDF advanced solver exports, general MATLAB/Python import and Studio GUI
dock/tile/caption behavior remain outside
the current explicit mappings. Review specs/checked_export_v0_2.txt and every
generated compatibility.json before interpreting target results.
Target binary64 arithmetic does not reproduce Studio's extended accumulator
overflow/underflow guards, and exact int64 overflow parity is not guaranteed;
the compatibility report records these limits. Validate exported sensitive
calculations using suitable tolerances and diagnostics.
