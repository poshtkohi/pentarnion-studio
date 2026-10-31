Native implicit residual DAEs and localized hybrid events (roadmap8 initial stage)

Examples: index_one_constraint.p, state_dependent_mass.p, hybrid_bounce.p,
pentarnion_implicit_decay.p. The optional Qt-free penta_dae module uses pinned
SUNDIALS7.9.0 IDAS with variable-order BDF and a dense linear solver. The local
idas-sdk adds static IDAS; it does not change the original CVODES/ARKODE SDK or
any global MSYS2 configuration. tools/bootstrap-idas.ps1 and
third-party/idas-dependencies.lock.json pin source/hash/flags/BSD notices.

Syntax: daesolve(residual,y0,yp0,options(algebraic=mask,...)). A residual begins
(real time, complete y parameters, complete yp parameters[,fixed parameters])
and returns n residual coordinates F(t,y,yp)=0. Both groups may comprise real
and Pentarnion scalars but may not split one Pentarnion across the y/yp boundary.
A Pentarnion contributes every coefficient in (1,i,j,k,ell) order. Initial data
may be scalars or vectors, including Pentarnions; records return real coordinate
columns. Nothing projects or silently discards a coefficient. n is bounded to32.
parameters binds trailing fixed residual arguments. Residual/event/reset bodies
are cached native callbacks, not interpreter AST calls inside solver steps.
Changing runtime parameters/initials/solver settings reuses an unchanged body.

The mask explicitly marks each real coordinate:0 differential,1 algebraic. The
adapter checks the local initialization matrix [F_yp on differential columns,
F_y on algebraic columns] for full rank after equilibration, and requires zero
initial F_yp columns on declared algebraic coordinates. Singular/higher-index
formulations are refused. This is a local initial index-one solvability contract,
not a general symbolic index proof, automatic index reduction or assurance that
later state-dependent operators stay nonsingular. Such later failures are errors.
No singular mass matrix is inverted. Regular time/state-dependent mass operators
are supported through F, with compiler-generated derivatives.

initialization="check" (default) preserves supplied initials and rejects a large
infinity norm residual. initialization="consistent" requests IDACalcIC with
IDA_YA_YDP_INIT: algebraic y and differential yp are corrected, while differential
y and algebraic yp are held fixed. Corrected values, initial_correction_norm and
initial_residual_norm are returned. initial_residual_tolerance (default1e-8) is
an explicit absolute residual-unit threshold; choose it appropriately for scaled
physical equations. index_tolerance (default1e-12) is the equilibrated rank cutoff.
The generated analytic Newton matrix is F_y+cj*F_yp. jacobian="finite_difference"
uses IDAS's numerical solver Jacobian; compiler AD remains available for the
local index/consistency preflight. Users never write a Jacobian.

Options include t0,t1 (strictly increasing), sample_step, rtol, atol, initial_step,
maximum_step, max_steps, max_evaluations, max_native_operations,max_wall_seconds.
The native integrator is serial within Studio's shared CPU reservation. Output
values and derivatives are sampled through IDAS dense output; maximum_sample_residual
is a diagnostic and may exceed the nonlinear residual at internal solver steps.
Retained samples/event arrays are jointly bounded to8,000,000 scalar coordinates.

Optional event is one real scalar compiled root function with the same (t,y,yp)
argument groups; event_parameters binds its trailing inputs. event_direction is
-1,0,1. event_terminal defaults to1 without a reset and0 when a reset is supplied.
A native reset returns new y coordinates followed by new yp coordinates (2*n),
with reset_parameters as its independent bindings. IDAS localizes the crossing,
the reset runs on that state, then IDAReInit restarts and the same initialization
policy validates/corrects the reset. event_times,event_before,event_after preserve
localized pre/post states separately from the regular sampling grid. Initial zero
roots follow IDAS's inactive-root policy; they are not fabricated as impacts.
max_events and minimum_event_interval enforce a clear chattering/Zeno stop policy.
Multiple simultaneous root functions, event priorities, arbitrary discontinuous
reset composition, saltation/event-time sensitivities and adjoints remain pending.
No gradient-through-reset or orbital stability claim is made by these trajectories.

Independent tests cover real/Pentarnion decay, a forced index-one constraint,
consistent initials, an integrated state-dependent mass equation, a pure time
constraint, higher-index refusal, localized bounce times/reset trajectories,
terminal roots, and cancellation/work/step/event limits. Native Windows GCC16.2
static IDAS was exercised. Linux uses the matching IDAS headers/library through
CMake but requires its own build/test evidence before claiming validation.
