clc;
ode decay(t,real x;real rate) { x'=-rate*x*x; }
native function real outside_section(real t,real x,real rate) { return x+10.0; }
native function real unchanged(real t,real x,real rate) { return x; }
native function real endpoint(real t,real x,real rate) { return .5*x*x; }
native function real integral_cost(real t,real x,real rate) { return x*x; }

record forward=hybridsolve(decay,outside_section,unchanged,1.0,
    options(parameters=options(rate=40.0),t1=.5,sample_step=.5,
            method="esdirk",rtol=1e-10,atol=1e-12));
display("Final state, expected 1/21: ",forward.values(end));
display("Rate and initial derivatives, expected [-.5/441,1/441]: ",
        forward.sensitivities(end,:));

record coarse=hybridadjoint(decay,outside_section,unchanged,endpoint,1.0,
    options(parameters=options(rate=40.0),t1=.5,sample_step=.5,
            method="esdirk",running_cost=integral_cost,rtol=1e-10,atol=1e-12));
record fine=hybridadjoint(decay,outside_section,unchanged,endpoint,1.0,
    options(parameters=options(rate=40.0),t1=.5,sample_step=.001,
            method="esdirk",running_cost=integral_cost,rtol=1e-10,atol=1e-12));
display("Objective .5*x(T)^2+integral(x^2): ",coarse.adjoint.objective);
display("Rate and initial gradients: ",coarse.adjoint.parameter_gradient,
        coarse.adjoint.initial_gradient);
display("Output grid leaves native checkpoint count unchanged: ",
        coarse.adjoint.checkpoints,fine.adjoint.checkpoints);
assert(abs(coarse.adjoint.parameter_gradient(1)-fine.adjoint.parameter_gradient(1))<1e-10);
% Change method to "erk" for the corresponding explicit native adapter.
