NATIVE DERIVATIVES AND HESSIANS

Open project.pprj and run main.p, then pentarnion_partials.p. Both scripts are
also valid penta-run inputs. The first call compiles a cached DLL/SO; tic/toc
automatically excludes its build time. The callback body is always native,
including explicit finite-difference mode; the solver never interprets it.

Expected principal main.p results:
  gradient = [19.6; -8]
  Hessian = [1170,-480; -480,200]
  H*[0.3;-0.7] = [687; -284]
  partial objective /partial a = 0.0016
  residual Jacobian = [4,1;2,-6]
  J*[2;-1] = [7;10]; J' *[2;-1] = [6;8]
  explicit-flow initial-value partial = exp(-0.5)
derivative_check(...,options(order=2)) independently compares native hyperdual
second derivatives with native finite differences and reports a passed flag.

pentarnion_partials.p computes state and parameter Jacobians as real5x5 maps.
energy(p)=norm2(p) gives gradient2*coefficients(p), Hessian2*I5 and H*v=2*v.
The five whole-element elementary functions use the published one-element
calculus, with analytic squared-pure-length series at zero. Mixed products keep
their written grouping. These are derivatives of a function/mapping, not
time-integrated ODE trajectory sensitivities (the latter remain advanced-odes).

Input packing follows declared parameters: Real contributes1 coordinate and
Pentarnion contributes5 in order(1,i,j,k,ell). Supply a typed tuple for mixed
arguments, a real vector of complete flattened coordinates, or one scalar/Pent.
options(parameters=options(...)) supplies trailing fixed declared arguments.
Default differentiation selects point coordinates only. options(wrt="A")
selects all coordinates of a named parameter; wrt=[1,2,...] selects one-based
flattened coordinates. Runtime values and wrt/solver options do not recompile.

method="analytic", "ad" or "automatic" names native automatic differentiation,
not symbolic algebra. method="finite_difference" is explicit central numerical
differentiation. Unsupported constructs and uncertified/nonsmooth branch points
report errors; no hidden finite-difference fallback is used. Hessian/HVP require
a real scalar objective. HVP seeds a direction and does not form a dense Hessian.
threads accepts a per-call0..64 maximum; current scalar derivative evaluations
are serial and derivative_check truthfully reports threads_used=1. Parallel
linear-algebra calls use their separately coordinated native CPU budget.

DIRECT ODE MODEL PARTIALS — ode_partials.p
Open the new script and Run File. It differentiates the ODE RHS natively at a
specified state/time; no time integration or trajectory sensitivity is implied.
States are leading inputs, parameters follow in declaration order, and time is
the final Real input using the ODE's original time name. Supply all model
parameters in options(parameters=options(...)); options(time=0.5) chooses time
(default0). The point must contain exactly the states, as a scalar/Pentarnion,
flat real vector, or typed tuple. Parameters/time cannot be slipped into the
point or overridden through parameters. Extra/missing parameter keys diagnose.

Default Jacobian columns are state coordinates. wrt="A" selects all five
coordinates of a Pentarnion parameter; wrt="t" selects the original time input;
wrt=[1,2,...] selects one-based flattened state/parameter/time coordinates.
Changing values, time or derivative selection reuses the native structural cache.
All expressions retain operand order and original Group nodes. JVP/VJP and
verification work for multiple/Pentarnion RHS outputs. Gradient/Hessian/HVP
require one scalar-real RHS; use Jacobians for vector-valued models.

For F=-a*y+t*y*y at y=2,a=3,t=0.5 the state partial is-1, parameter partial-2,
and time partial4. Its Hessian in [y,a,t] coordinates is
[1,-1,4;-1,0,0;4,0,0], and H*[1;2;3]=[11;-1;4]. The Pentarnion linear example
has J_p=6*I and J_A=4*I. A bracketed nonlinear Pentarnion RHS passes an independent
finite-difference derivative check. Two subplots show sampled native partials.
This feature differentiates F(t,p,theta), not the integrated solution p(t).
Forward/adjoint trajectory sensitivities remain roadmap advanced-odes.
