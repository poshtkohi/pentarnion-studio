ROOT FINDING AND PENTARNION EQUILIBRIA

Open project.pprj in Studio and Run main.p. It prints solver reports and opens
two subplots. Native callbacks compile before solving; later changes to brackets,
guesses, tolerances and fixed parameter values reuse their structural cache.
tic/toc excludes compilation. penta-run intentionally disables figures, so use
Studio for the complete example.

Expected principal results:
  scalar root with a=2: sqrt(2), about1.414213562373095
  changed a=3: sqrt(3), about1.732050807568877
  system root: [1,1]
  both ordered Pentarnion equations recover target1+i-j+0.25*ell
  map fixed point:2+2*j
  autonomous driven-decay equilibrium:3
  no_real_root has converged=0, residual_norm>=1; it is not a root

root uses Boost.Math TOMS748 on a finite sign-changing bracket. It assumes the
function is continuous there; finite sampling cannot establish continuity or
find all roots. A small final bracket alone does not set converged=1: the scaled
residual must also satisfy ftol. Exact endpoint roots are accepted.

rootsolve uses native SUNDIALS KINSOL, method="line_search" (default) or "newton".
The compiler supplies the complete generated real-coordinate Jacobian when
jacobian="analytic"/"ad"/"automatic". jacobian="finite_difference" instead
requests KINSOL's difference Jacobian of the same compiled native callback.
Every evaluation remains native, including finite differences.

Unknown arguments occupy the leading declared parameters. A scalar/Pentarnion
initial value supplies one parameter; a real vector packs complete leading real
coordinates, including five coordinates for each Pentarnion parameter. A typed
tuple supplies mixed scalar/Pentarnion parameters. Fixed trailing parameters
come from options(parameters=options(name=value,...)); valid constant parameter
defaults can also supply them. Captured anonymous environments are not compiled:
pass fixed parameters explicitly. Native callbacks accept the documented pure
scalar algebra/math subset and scalar or multiple typed returned values.

equilibrium uses an autonomous ODE's native RHS=0; an RHS referencing time is
rejected. It is a nonlinear solve, not integrated ODE trajectory sensitivity.
fixedpoint uses native G(x)-x and Jacobian DG-I. Left/right products and all
explicit parentheses inside callback expressions remain unchanged.

Options: method, jacobian, parameters, xtol(default1e-12), ftol(default1e-10),
max_iterations(default200,maximum100000), max_evaluations(default10000,3..1000000),
max_wall_seconds(default60,maximum3600), threads(0..64; current solver uses1),
residual_scale(positive coordinate vector), variable_scale(positive KINSOL vector),
max_newton_step(default0 automatic, KINSOL only). Automatic Newton step limit is
1000*max(1,scaled initial L2 norm), avoiding a degenerate limit at zero guesses.
rtol(default1e-12) applies only to scalar brackets. KINSOL xtol is a scaled-step
tolerance, not an absolute error estimate. Scaling changes the stopping test:
scaled_residual_norm=max(abs(F).*residual_scale); raw residual_norm is also shown.

Reports include x, residuals(real-coordinate column), residual_norm,
scaled_residual_norm, converged, status, message, flag, iterations, evaluations,
jacobian_evaluations, backend_evaluations, native_evaluations, step_norm, bracket, method, jacobian,
cache_hit/cache_key and requested_threads/threads_used. Scalar TOMS748 iterations
is Boost's iteration/evaluation counter; KINSOL iterations counts Newton steps.
evaluations counts host F/J requests; native_evaluations counts actual generated
DLL entries, including multiple AD passes for a Jacobian. backend_evaluations is the
solver's own counter. Cached residuals at exactly identical final coordinates
can supply final verification without a redundant pure callback evaluation.

Invalid arguments/domains, cancellation and exhausted hard evaluation/wall/work
budgets raise diagnostics. Ordinary KINSOL failure returns converged=0 and status
plus backend flag/message. Small steps, stagnation and iteration limits do not
establish a root. Current dimension bound:128 real coordinates. The root module
loads lazily from application modules/, is Qt-free and stays resident to shutdown.
CPU solve/Jacobian evaluation is serial and launches no competing worker pool.

