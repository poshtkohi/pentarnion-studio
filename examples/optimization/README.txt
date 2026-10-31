NATIVE OBJECTIVE OPTIMIZATION

Open project.pprj and Run main.p in Studio. All objectives and constraints are
compiled native functions, with generated gradients when a selected algorithm
requests them. Native finite_difference is explicit; no interpreted fallback.

Expected examples:
- LBFGS solves Rosenbrock from[-1.2,1] to approximately[1,1].
- SLSQP minimizes a quadratic with g(x)=x+y-1<=0 and h(x)=x-y=0 at[0.5,0.5].
- CRS2-LM, bounds[-1,1],seed42 finds a candidate near0.25. A finite global search
  is not a certificate of the global minimum; its termination budget is reported.
- Subplots retain objective and constraint-violation histories.

Available algorithms: lbfgs,bobyqa,nelder_mead,slsqp,cobyla,direct,crs2,isres.
SLSQP/COBYLA/ISRES accept native nonlinear inequalities/equalities. Other
algorithms reject unsupported nonlinear constraints. Global algorithms require
finite bounds on every coordinate. BOBYQA requires at least2 coordinates.

All solvers use the host CPU reservation. Current NLopt algorithms are serial;
threads(n) and per-call requests remain upper bounds, not forced pool sizes.
Random seeds are serialized within the native NLopt module to make repeated
identical seeded requests reproducible. Objective, constraint and actual native
body evaluation counts are distinguished. max_evaluations bounds total callback
requests, including nonlinear constraints. Histories are bounded/truncated to
history_limit; extra history is not silently allocated.

Default derivative-free algorithms use parameter tolerance and evaluation/time
budgets; ftol/ftol_abs are opt-in because equal simplex values can occur away
from a minimizer. A returned converged flag describes backend stopping criteria
and independently checked feasibility, never a global optimality proof.
