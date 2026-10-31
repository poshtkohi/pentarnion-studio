REAL HIGH-ORDER FINITE ELEMENTS (MFEM4.10)
Open project.pprj and run a descriptive .p file explicitly.

manufactured-quartic-solution.p: unstructured real square mesh, quartic exact
solution, H1 P4 full nodal DOFs, sampled height plot, residual/L2 diagnostics and
runtime-parameter cache reuse.
mesh-refinement-convergence.p: real P1 mesh refinement and P2 order comparison.
variable-coefficients-and-boundaries.p: native variable diffusion and nonzero
Dirichlet data, reaction term, manufactured linear exact solution.

These are stationary real scalar elliptic PDEs, not time-dependent or
Pentarnion-valued high-order FEM. The existing structured finite-difference/
P1 time-dependent and Pentarnion solvers remain separate APIs.
MFEM calls are serial and serialized under Studio's shared CPU reservation.
Every connected mesh component needs at least one selected Dirichlet edge.
Unselected external tags have homogeneous natural Neumann conditions.
No pure-Neumann mean constraint, adaptive mesh error estimator, distributed
PETSc/MPI backend, or Linux execution is claimed by these examples.
