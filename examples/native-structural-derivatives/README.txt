COMPILER STRUCTURAL DERIVATIVES AND CONSISTENT DAE INITIALS

structural-sparsity-at-zero.p
  Shows native CSR Jacobian values at a zero of a derivative. The compiler
  retains the structural entry; changing inputs/parameters reuses the DLL/SO.

consistent-time-dependent-algebraic-constraint.p
  Diagnoses a nonlinear index-one residual, projects its initial conditions and
  computes full algebraic/differential initial derivatives including explicit
  time dependence. The result is passed directly to native IDAS daesolve.

higher-index-structural-diagnostics.p
  Shows conditional structural offsets of an index-three chain. It deliberately
  does not ask the local index-one initializer to reduce it. The independently
  validated constant-pencil lineardaesolve adapter solves this stated example.

tensor-constrained-evolution.p
  A rank-three native tensor residual, automatic consistent y/yp, and IDAS
  integration of x'=-x with z=x squared. Full entry shape/typing is retained.

tensor-native-derivatives-roots-and-fit.p
  Public native tensor Jacobian/Hessian/HVP/AD3, nonlinear roots, least squares
  and optimization. Solver results retain the complete tensor descriptor.

Every numerical residual/time/state derivative is compiled and cached. The
structural scan reads the checked AST; it never evaluates source at a sample to
guess nonzeros. A structural index is a candidate, not a general nonlinear proof.
See specs/structural_derivatives_and_dae_v0_2.txt for shape/resource and mathematical
limits. All five examples execute with the standard default operation budget.
