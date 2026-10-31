SURFACE HEAT AND REACTION-DIFFUSION

surfaceheat(mesh,initial[,options]) solves on genuine2D P1 triangular surfaces
embedded in2..5 real coordinates, with the induced Euclidean metric. It uses a
consistent mass matrix (not mass lumping), backward Euler diffusion and IMEX
Euler when a nonlinear reaction is supplied. Scalar diffusivity is real,
finite, nonnegative and time-independent; it may be a compiled spatial field.
Reaction is explicit: choose/validate a time step suited to its dynamics.

Initial data may be one real/Pentarnion value, one value per vertex, or a
compiled function of every embedding coordinate returning real/Pentarnion.
Explicit realN×5 initial coordinate matrices require options(width=5).
The reaction signature is reaction(state,t,x1,...,xd[,runtime parameters]);
the result has the same real/Pentarnion domain as state. Every initial,
diffusion and reaction evaluation runs natively. Initial_parameters,
diffusion_parameters and parameters respectively bind trailing function data.
Changing those values or time controls reuses the cached shared code.

Default boundary condition is natural zero Neumann. Constant tagged Dirichlet
data uses options(dirichlet=options(tag1=value,tag2=value,...)); initial data
must already satisfy it and conflicting corner values are rejected. General
time-dependent/Robin data and fully implicit nonlinear reactions are not part
of this bounded first surface solver.

Parallel initial/element/quadrature assembly uses Studio's shared CPU budget,
including nested native thread settings. Sparse factorization/solves are
serial and reused for equal step sizes. threads=0 selects the bounded runtime
CPU policy. Deterministic element reduction preserves numerical semantics.
An AMD-order symbolic preflight limits factor fill to four million entries
and charges its elimination work before numerical factor allocation. Native
cancellation is checked throughout assembly/preflight and between Eigen
factorization/solve calls; an individual Eigen call finishes before it returns.

The solution retains mesh, t, flattened values, all coefficient means and
consistent-mass energy. surfacecoeff(solution,c[,frame]) selects coefficient
c=0..4 and optional one-based frame. Without a frame it returns samples×nodes;
with a frame, nodes×1. Requested sample times linearly interpolate between
implicit time steps; choose matching sample_step/time_step for step states.
Mass/energy diagnostics are descriptive: reactions/Dirichlet forcing can
change both, while closed pure diffusion should conserve mass and dissipate
the discrete energy. The sphere example has the independent eigenmode
exp(-2t)x; improve mesh and time resolution to check convergence.
