HOLONOMIC CONSTRAINED MECHANICS

constrainedmechanics(@potential,@constraints,q0,v0[,options]) is an explicit
real-coordinate separable mechanics interface:
    H(q,p)=1/2 p^T M^-1 p+V(q),  g(q)=0,
    M q''=-grad(V)-G(q)^T lambda,  G=dg/dq.
It supports1..16 real configuration coordinates,1..n independent autonomous
real constraints, and a constant real symmetric positive-definite mass.
Mass may be a scalar, diagonal vector or full n-by-n matrix.

Potential returns one explicitly real scalar; constraints returns a real
scalar or tuple. Both begin with one complete q-coordinate group, followed by
fixed parameters bound through parameters and constraint_parameters options.
Configuration groups may consist of real/Pentarnion scalar parameters, but
all physical configuration coordinates are explicitly flattened real values.
A Pentarnion initial value supplies its five ordered coefficients; no
automatic mechanical meaning, mass, metric or energy identity is inferred.

The compiler generates the potential gradient, constraint Jacobian and
constraint Hessians in cached native shared libraries. Changing initial data,
mass, integrator controls or trailing parameter values reuses those bodies.
Parentheses in any ordered algebra expressions retain their usual semantics.

method="rattle" (default) uses constrained velocity Verlet: SHAKE position
iteration followed by a mass-metric tangent velocity projection. method="shake"
uses position-constrained Verlet without the final velocity projection. Its
reported endpoint v is explicitly unprojected (the velocity_semantics field),
so tangent_residual and velocity_error need not vanish. Use RATTLE for tangent
endpoint velocities and constrained Hamiltonian phase-space diagnostics.

initial_policy="reject" is the default: q0 and v0 must already satisfy g=0
and Gv=0 within tolerance. Explicit initial_policy="project" performs bounded
local mass-normal position and tangent velocity projections, reporting the
actual initial_q_correction/initial_v_correction and original residuals. This
is a local consistency construction, not a global nearest-point guarantee.
Dependent/zero constraint gradients and numerically singular mass/projection
operators are rejected. Newton iteration has bounded backtracking and clear
failure diagnostics; reduce time_step when a position projection fails.

Histories include t,q,v,p, total/kinetic/potential energy, raw constraints g,
raw tangent_residual Gv, scaled position_error/velocity_error, and multipliers.
Position error is max(abs(g_i)/max(abs(G_i,:)))/(1+max(abs(q))). Velocity error
is max(abs(G_i*v)/max(abs(G_i,:)))/(1+max(abs(v))). This prevents arbitrary
constraint equation scaling from controlling the requested tolerance.
max_relative_energy_drift uses abs(E-E0)/(1+abs(E0)); conservation is a numerical
diagnostic, never an unconditional exact-energy guarantee.

lambda is the instantaneous acceleration-constraint coefficient generated
using Hessians of g and the reported state. Under RATTLE it is the physical
multiplier in Mq''=-grad(V)-G^T lambda; for SHAKE inspect the documented
unprojected velocity before interpreting that acceleration diagnostic.
position_multipliers and velocity_multipliers are the algorithmic coefficients
in q+=q+h*v-h^2/2 M^-1(grad(V)+G_old^T*lambda_q) and
v+=(q_new-q_old)/h-h/2 M^-1(grad(V_new)+G_new^T*lambda_v).
Initial algorithmic multipliers are zero; position_iterations and
position_correction report retained step projection work.

sample_step must be an integer multiple of time_step. Retained outputs are
actual constrained step states, with a final short step if necessary; no
off-manifold interpolation is performed. CPU reservation follows Studio's
shared thread budget; these small dense kernels run serially and report1.
Independent native function compilation uses the existing bounded compiler
plan. Bounds cover coordinates, retained histories, steps, evaluations,
iterations, native work, wall time and cancellation.

This specialized regular holonomic formulation does not claim generic
high-index DAE reduction, time-dependent/state-dependent mass operators,
nonholonomic conventions, contact/impacts or inequality constraints.
