PERIODIC SHOOTING, FLOQUET STABILITY AND PSEUDO-ARCLENGTH BRANCHES

Open project.pprj; run main.p or equilibrium_fold.p in Studio. The numerical
commands also work in penta-run; omit figure/plot statements for headless use.

periodicorbit(model, initial, options(...)) refines the initial full real state
and positive period with Newton shooting plus a phase condition. Each Pentarnion
state contributes all five coordinates. Supply every declared fixed parameter:
parameters=options(mu=1.0,...). Optional state_scale is a positive vector in
declaration/coefficient order; period_scale is positive. They scale shooting
coordinates/closure and the phase condition. Physical and scaled closure and
amplitude are reported separately. Phase direction defaults to the native RHS
at phase_reference (default initial); a zero phase direction is invalid.

The monodromy is generated through native analytic tangent dynamics, not finite
differences of sampled trajectories. The multiplier closest to 1 is identified
as the autonomous neutral mode. Stable/unstable evidence requires convergence,
the neutral error within stability_tolerance, and a transverse spectral gap.
Extra unit-modulus modes remain inconclusive. A numerically closed trajectory
does not establish its fundamental/minimal period or rigorous orbital stability.
Equilibrium-like negligible scaled amplitude is not accepted as a nontrivial
cycle. A singular Newton corrector or failed line search returns nonconvergence.

continueequilibrium(model, initial, options(parameters=...,parameter="mu",...))
tracks equilibria using a tangent predictor, augmented Newton corrector and
pseudo-arclength. continueperiodic uses the same method on full flow closure,
phase and period. The periodic branch uses a fixed phase section through its
refined seed. It may need a different seed/phase section on distant branches.
step is signed for the initial parameter direction; min_step/max_step bound
adaptive arclength magnitude. min_parameter/max_parameter bound tracking.
For a Pentarnion-valued parameter, select component=0..4 explicitly; all other
coefficients stay fixed. All state and selected parameter derivatives are
compiler-generated native code; changed values/options reuse compiled bodies.

Current bounds: autonomous ODEs, 1..32 real state coordinates (periodic needs
at least two), BDF/Adams for tangent integration, 2..1000 continuation rows and
1..100 Newton iterations. Callback bodies use the native ODE/function subset.
max_seconds, max_steps, cancellation and the shared host work quota are enforced.
Shooting scales apply to seed refinement; branch arclength currently uses the
physical coordinates and parameter, so choose comparable branch units.

Branch measure is the largest real equilibrium eigenvalue or the largest
transverse multiplier modulus. fold_candidate marks a sign change in the
parameter component of the arclength tangent. hopf_candidate matches complex
eigenmodes and marks a real-part sign change. period_doubling_candidate matches
real Floquet multipliers across -1. These are sampled candidates only; close
eigenvalue collisions and coarse steps need rechecking. bifurcation_refined=0:
dedicated fold/Hopf/period-doubling refinement and certified candidate plotting
remain future Task 3 work. fundamental_period_verified is likewise always 0.

main.p compares a known radial normal form against radius=sqrt(mu), period=2*pi
and radial multiplier exp(-4*pi*mu). equilibrium_fold.p verifies mu=-x^2 and
eigenvalue 2*x, continuing from stable negative x through a parameter fold to
unstable positive x. The plots retain the actual residual/branch data.
