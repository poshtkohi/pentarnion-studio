NATIVE EVOLUTIONARY OPTIMIZATION

Open project.pprj and run main.p. It exercises all four algorithm families,
including both jDE/iDE parameter-adaptation variants. Four scalar searches have
the independent optimum [0.25,-0.5], objective zero. The script checks error,
reuses the same native callback with a changed runtime parameter, explicitly
refines a candidate with existing NLopt LBFGS, and plots convergence histories.

The second figure shows NSGA-II for f1=x^2 and f2=(x-2)^2, x in[0,2]. Every
point in this interval is Pareto optimal. Reported objective pairs are an
approximation to that continuum; there is no single preferred compromise.

pentarnion_and_log.p searches five real Pentarnion coefficients for the known
target1+i, and demonstrates positive logarithmic coordinates with target100.
All bodies execute cached native callbacks; no interpreted solver fallback.

History columns: generation, cumulative native objective requests, best first
objective over all evaluated candidates, normalized population coordinate spread.
For multiple objectives, x/objective is a lexicographic representative. Use
pareto_x/pareto_f for tradeoffs. Finite budgets and population collapse are not
global-optimum certificates. Same-build seeded tests cover repeatability across
the tested thread counts; cross-platform bit identity is not promised.

Native CMA-ES/NSGA-II batches and initialization use Studio's CPU pool. jDE/iDE
and GA generations remain serial in the selected upstream implementation.
Stateful evolutionary checkpoint/resume, constrained/mixed-integer evolution,
island migrations and automatic multi-basin local refinement remain future.
