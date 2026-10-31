advanced-odes — NATIVE ADVANCED ODEs AND SENSITIVITIES

Open project.pprj in Studio. Run each script separately:
  main.p: native directional/terminal event localization, CVODES parameter and
          initial-state trajectory sensitivities, native dense interpolation,
          stored-transient exclusion and backward integration; four subplots.
  adjoint.p: checkpointed CVODES terminal-objective gradients with respect to
             every model parameter and every initial-state coordinate.
  mass_and_times.p: constant real-coordinate mass matrix and arbitrary exact
                    requested observation times, with step/order controls.

Expected values:
  decay y'= -0.7*y, y(0)=1 gives y(t)=exp(-0.7*t).
  half-height crossing is t=log(2)/0.7 =0.990210257942779.
  dy/dr=-t*exp(-r*t); dy/dy0=exp(-r*t).
  terminal objective J=0.5*y(T)^2+3*r for y'=r*y,y0=2,T=1.2:
      dJ/dr=T*y(T)^2+3; dJ/dy0=y(T)*exp(r*T).
  mass=[2] changes decay rate from0.7 to0.35; y(2)=2*exp(-0.7).

Every model/event/terminal objective/parameter-partials callback is native cached
DLL/SO code. Event and terminal-objective signatures are
  (real time, declared states..., declared model parameters...)
with matching scalar real/Pentarnion types. Events return one real value or a
real tuple of at most16 values. The event function must be continuous around a
crossing; rootfinding detects crossings, not every grazing/tangent zero.
Changing values, initial conditions, time interval, method, tolerances, event
flags or sensitivity selectors reuses the unchanged structural kernel.

Named odesolve options added:
  events=callback, event_direction=-1/0/+1 or a matching vector,
  event_terminal=0/1 or a matching vector;
  sensitivities="none"/"parameters"/"initial"/"all",
  sensitivity_parameters=[...] (one-based flattened parameter coordinates),
  sensitivity_initial=S0 (n-by-ns real matrix),
  sensitivity_method="simultaneous"/"staggered",
  sensitivity_error_control=0/1;
  initial_step,max_step,min_step,max_order;
  output_times=[...] (strictly ordered within t0,t1),
  dense_output=0/1,max_dense_coordinates=1..8000000;
  mass=M (constant nonsingular real n-by-n coordinate matrix).

sample_step is always a positive magnitude; t1<t0 requests backward integration.
Event directions use increasing physical time even when integrating backward.
Terminal roots append the accurately localized root time/state to the solution;
nonterminal roots are returned separately through odeevents(solution).
Event callbacks are not reset/jump handlers. Discontinuous resets require a
separate hybrid-system API rather than pretending ordinary sensitivity equations
include event-time or jump derivatives. Forward sensitivities at events refer to
fixed-time smooth trajectories; they are not derivatives of crossing times.

odesens(s) returns a real matrix with one sample per row. Columns contain n state
coordinates for each selected direction. Parameters precede initial directions
for sensitivities="all". odesens(s,k) selects one direction. A Pentarnion state or
parameter has five explicit real coordinates in(1,i,j,k,ell) order.
CVODES BDF/Adams integrates S'=J_x*S+J_theta, with declared seeds and optional
sensitivity error control. ERK/ESDIRK sensitivity integration is currently rejected.
No derivative passes through an opaque double-valued solve by optimizer AD.

odeadjoint(model,initial,options(parameters=...,objective=callback,t0=...,t1=...))
uses actual CVODES checkpoint/replay and backward adjoint quadrature for one real
terminal objective. It returns objective,parameter_gradient,initial_gradient,
backward_steps,checkpoints,solution,cache_hit,cache_key. It supports BDF/Adams and
constant nonsingular masses. The fixed terminal-time objective can depend directly
on parameters. Running/integral objectives, event-time adjoints and jump maps are
not silently approximated by this API.

deval(s,times[,state_name]) now evaluates retained native internal-step
interpolants. It uses CVODES Taylor polynomials or DP5 quartic / ESDIRK cubic interpolants,
whose interpolation degree is independent of the user output sample spacing.
Dense segments retain the native coefficients after the integrator is freed.
Legacy saved solutions without these coefficients retain the documented sample
Hermite path. dense_output=0 uses that path when endpoint slopes are available.
Native dense output is interpolation, with accuracy governed by method, tolerance
and step size; it is not exact symbolic evaluation or a uniform global error bound.

odewindow(s,start[,stop]) selects stored output samples in the requested absolute
interval, following forward/backward integration order. The first sample can be
after the start boundary. It retains matching sensitivities, event data and dense
segments; it does not rerun the model or alter the original numerical solution.
odestats(s) exposes actual steps/RHS/Jacobian/event/sensitivity counts, error-test
failures, nonlinear iterations, last-step/order, timing, cache key/hit, terminal
status, dense-segment count and sensitivity selection.

Solution persistence preserves native dense coefficients, sensitivity matrices,
event metadata and descending time order. Retained values, endpoint slopes,
sensitivities, event data and dense coefficients share a combined32M-coordinate
budget including time samples. State/sensitivity sampling retains its8M bound;
max_dense_coordinates imposes an additional8M dense-only bound. Samples,
steps, event counts, wall time and cancellation remain bounded. A mass matrix
must be explicit, real, constant and numerically nonsingular; DAEs are rejected.
