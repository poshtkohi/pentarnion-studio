NATIVE REAL AND PENTARNION GRAPH DYNAMICS

networksolve(local_ode,G,initial,options(...)) evaluates one cached native ODE
kernel for all nodes. Rows of initial identify stable nodes; each row contains
all local real coordinates, or the matching Pentarnion matrix entries. Mixed
real/Pentarnion ODEs can use a flat real row containing the full typed coordinates.
state_names/state_widths describe the exact declaration order. The same native
structural key is used by ordinary odesolve: changing node count, topology, real
weights, parameters, gamma, initial data or solver settings does not rebuild an
unchanged node body. Generated local Jacobians are available in that binary.

method="rk4" uses explicit fixed-step RK4, with step as the maximum actual step
and exact stored sample times. Its Jacobian report says compiled_not_used_rk4;
refine step and compare results for nonlinear experiments. rtol/atol are rejected
for RK4 rather than silently ignored.

method="bdf" uses adaptive CVODES BDF, with rtol/atol and an optional maximum step
(step=0 lets the integrator choose). Stored samples come from the native solver
interpolant. Generated local Jacobians and incoming CSR graph coupling produce
matrix-free Jacobian-vector products. Local block-Jacobi LU preconditions SPGMR;
no global dense Jacobian is assembled. krylov_dimension controls bounded Krylov
storage. Statistics report actual accepted steps, largest/last step, local Jacobian
evaluations, J*v calls, block factorizations and solver iterations/failures.

For an edge source->target, target receives
  diffusion[c]*weight*(source[c]-target[c]).
Undirected edges act both ways; self loops contribute zero. Stable physical edge
order controls each node's summation. Weights/diffusion are nonnegative real
numbers by default. coupling="general" explicitly permits signed real operators;
no positivity or stability claim is then inferred. Pentarnion-valued coupling
operators with left/right grouping require a separate explicit formulation.

Native local RHS/coupling tasks use Studio's shared CPU budget. threads=0 resolves
automatically; an outer numerical task forces inner network work into its reserved
slot. All scientific evaluation occurs on native workers, never in the GUI or
an interpreter per node/step. Cancellation, wall/step/work limits and estimated
memory caps are checked. Results retain every coefficient, not just a projection.

networkcoeff(result,node,coordinate) uses one-based node and flat local coordinate
indices. For a single Pentarnion state, coordinates1..5 mean scalar,i,j,k,ell.
It returns an exact stored real column suitable for plotting. values are stored
sample-major, node-major, coordinate-fastest. means retains each coordinate mean.
synchronization is sqrt(mean_node sum_coordinate (node-mean)^2), an explicitly
Euclidean coefficient-space diagnostic, not an unspecified algebraic norm.

real_network_diffusion.p has an independent two-node exponential reference.
pentarnion_network_diffusion.p retains and displays all five conserved means.
local_oscillator_network.p demonstrates generic nonlinear two-state node bodies
and a reproducible twelve-node experiment; it makes no universal chaos/stability
claim from a plotted finite trajectory.
stiff_network_bdf.p compares two stiff exponential modes with an independent
analytic solution and reports the native iterative/Jacobian statistics.
