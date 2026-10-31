NATIVE SOLVER SUMMARY SENSITIVITY

These examples use the existing native Morris/Sobol module with native solver
adapters. No interpreted ODE/map/PDE summary callback is executed per sample.

main.p: exact additive ODE final-state effects; Morris[1,2]. Optional Sobol
first/total reference[0.2,0.8] for independent uniform[0,1] parameters.
iterated-map-effects.p: five native iterations; exact Morris[5,10].
reaction-diffusion-effects.p: constant1D field; source/diffusion effects[.1,0].
two-dimensional-field-effects.p: constant2D field; source/dx/dy effects[.1,0,0].
ordered-pentarnion-map-effects.p: complete five-coordinate native map, preserving
left multiplication j*p while selecting one real output coefficient for analysis.

The supplied scripts are small enough for the normal interpreter/native quotas.
More samples, larger spatial grids and tighter settings need explicit budgets.
threads requests a shared maximum; actual threads_used can be lower for tiny jobs.
Each worker has its own native solver/state; inner PDE work stays in its one
reserved slot. No separate or multiplying thread pool is created.

parameter_coordinates is one-based among flat trailing parameters. Pentarnion
parameters have five slots. PDE1 appends diffusion; PDE2 appends diffusion_x/y.
parameter_layout.model/solver record offsets without naming collisions.
coefficient is one-based among complete ODE/map coordinates or PDE coefficients;
Pentarnion1=scalar,2=i,3=j,4=k,5=ell. This differs explicitly from zero-based
pdecoeff. PDE node is one-based, x-fast for2D. Spatial summaries use the final
field; temporal summaries use discrete stored samples after the chosen transient.

No invalid/failed simulation is replaced by zero. Correct the domain/numerical
settings if a sample fails. Morris/Sobol measure sensitivity under the stated
sampling assumptions; they do not establish parameter identifiability.

See specs/solver_sensitivity_v0_2.txt. Isolated Windows native29 checks pass,
including analytic ODE/map/FD/FE/PDE2 references, every ordered Pentarnion PDE
coefficient, parallel determinism/use, failure rejection and cancellation.
Full runtime/example/platform acceptance is recorded separately in HANDOFF.
