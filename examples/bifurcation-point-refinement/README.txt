NATIVE BIFURCATION POINT REFINEMENT

main.p: true equilibrium arclength branch plus separate augmented-Newton fold
refinement. The analytic point is x=mu=0 with nonzero left-null parameter and
quadratic coefficients. Generated native AD2 supplies the entire corrector.

moving-equilibrium-hopf.p: the equilibrium moves with mu. The correct eigenvalue
crossing is1.5; using only the fixed-state J_mu would give the wrong answer.
Native Hessians supply the missing J_x*(dx/dmu) term. Criticality/first Lyapunov
coefficient is explicitly uncomputed; there is no hidden third-derivative claim.

four-dimensional-flip.p: independent autonomous normal form with an exact2*pi
cycle. Its simple transverse multiplier is-exp(2*pi*mu), so the crossing occurs
at mu=0 with derivative-2*pi. Native tangent flow, full-state closure and phase
are refined together. The augmented corrector uses explicitly documented
centered differences of that native flow; crossing h/h2 agreement is printed.
The Floquet crossing alone does not establish cubic criticality/emerging branch.

pentarnion-fold.p: all five state coefficients and all five parameter slots
remain present. Only the explicitly selected scalar A coefficient varies; the
other four parameter coefficients are retained unchanged. No projection becomes
a substitute for the full-state equations.

The lazy penta_bifurcation DLL/SO uses a versioned native ABI and the existing
cached compiler/SUNDIALS core. No interpreter evaluates a corrector/flow sample.
One shared CPU slot is reserved for these bounded small Eigen calculations.
max_bytes is a buffer forecast, not an exact allocator/RSS cap. Work/time/step
quotas and cancellation apply. More than one center/critical mode prevents an
ordinary codimension-one candidate label. Numerical candidates are not rigorous
interval-certified bifurcation proofs.

See specs/bifurcation_refinement_v0_2.txt. Isolated native Windows27 checks pass,
including exact normal forms, nonconstant equilibrium motion, degenerate/extra
center rejection, every Pentarnion coefficient, the exact flip reference, caches,
budgets and cancellation. Full integration/platform acceptance is in HANDOFF.
