Native mechanics: bounded initial stages of second roadmap13A/B/C/D/F

Examples: newton_kepler_verlet.p, lagrangian_pendulum.p,
hamiltonian_midpoint.p, pentarnion_ordered_force.p. The Qt-free optional
penta_mechanics DLL/SO uses compiled cached scalar/tuple model callbacks and
compiler AD2; there is no AST interpretation inside time steps or Newton loops.

Syntax: mechanics(model,q0,second0[,options]). form is newton (default),
lagrangian or hamiltonian. q0 and second0 contain the same1..8 real coordinates;
a Pentarnion contributes all five coefficients in (1,i,j,k,ell) order. Real or
Pentarnion callback groups are complete and separated. The leading callback
argument is real time, followed by q and, when required, velocity or momentum;
parameters binds trailing runtime arguments. Changing parameters/initials/numerical
settings reuses unchanged native bodies. Products keep explicit operand grouping.

Newton: second0 is velocity. Positive constant mass (scalar or one value per real
coordinate, default1) gives q'=v, v'=force/m. force_layout="position" (default)
requires the signature (t,q[,parameters]); force_layout="state" requires
(t,q,v[,parameters]). The callback returns n force coordinates, including one
Pentarnion for five coordinates. method="rk4" is general fixed-step RK4;
method="verlet" is velocity-Verlet and requires the position-only signature,
so a velocity-dependent force cannot accidentally use it. Time-dependent position
forces are allowed; no unconditional conservation or autonomous symplectic-energy
claim is made. Output p=mass*v and kinetic_energy=(1/2)sum mass*v^2 are explicitly
real Euclidean coordinate diagnostics. Total potential energy is not inferred
from an arbitrary force; the Kepler example defines its potential separately.

Lagrangian: second0 is velocity; model returns one explicitly chosen real scalar
L(t,q,v). The compiler evaluates Lq,Lv,Lvv,Lvq,Lvt using AD2. Native code solves
Lvv*a=Lq+Q-Lvq*v-Lvt. A velocity Hessian that fails regularity_tolerance (default
1e-12 relative pivot threshold) is refused as singular/constrained; no implicit
mass inverse or invented regularization is used. Optional force/force_parameters
supply a separately compiled generalized force Q(t,q,v). Output p=Lv and
energy=sum v*Lv-L follow these declared real functions. Coordinate-dependent
regular Lvv is supported. Fixed RK4 is currently supported for this form; no
third-derivative-based analytic acceleration Jacobian is falsely advertised.

Hamiltonian: second0 is canonical momentum, not velocity. A real scalar H(t,q,p)
gives q'=Hp, p'=-Hq. Output v is Hp while p retains the canonical state. Default
method="midpoint" integrates the implicit midpoint equations using native Newton
iterations and the exact compiler AD2 Hamiltonian Hessian (I-dt*J*H''/2). A failed
Newton limit/singular Newton matrix is an error with a smaller-step suggestion;
there is no silent method fallback. max_newton_iterations (default20),
newton_tolerance (default1e-11) and regularity_tolerance control the iteration.
Fixed RK4 is also available. A general nonseparable Hamiltonian is supported;
Verlet is not silently applied to it. Energy is the supplied H. Time dependence
or generalized forcing can change defined energy physically; drift is a sampled
numerical diagnostic, not an automatic conservation assertion.

Common options: form,method,parameters,t0,t1,time_step,sample_step,
max_steps (default1,000,000; cap10,000,000),max_evaluations,max_native_operations,
max_wall_seconds. Scalar output arrays t,q,v,p use one real coordinate per column;
energy/kinetic_energy and lagrangian/hamiltonian appear only when defined by the
selected form. Reports include second_input, coordinate_dimension, model and
force evaluations, Newton iterations, native_jacobian, cache identities and serial
threads_used=1 within Studio's shared CPU reservation. Outputs jointly fit the
8,000,000 coordinate budget. Time-step refinements are required for accuracy;
no adaptive integrator, arbitrary unit checker or hidden constraint solver exists.

Independent tests compare analytic oscillators, RK4/midpoint convergence, a
nonseparable quadratic Hamiltonian, pendulum energy, coordinate-dependent L mass,
forced damped motion, Kepler/Verlet angular momentum, five-coordinate forces,
singular/method errors and cooperative cancellation. General holonomic constraints,
SHAKE/RATTLE, nonholonomic conventions, articulated bodies/contacts and automatic
units/work-balance histories remain future stages. The separate daesolve adapter
handles supported explicit index-one residual formulations; it does not make an
unreduced high-index constrained Lagrangian supported automatically.
