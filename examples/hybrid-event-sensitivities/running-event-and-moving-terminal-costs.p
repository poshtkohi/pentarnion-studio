clc;
ode flow(t, real x; real a, real b, real c) { x' = a; }
native function real section(real t, real x, real a, real b, real c) {
    return x-b;
}
native function real reset(real t, real x, real a, real b, real c) {
    return x+c*t;
}
native function real final_cost(real t, real x, real a, real b, real c) {
    return t+.5*x*x+.3*c;
}
native function real running_cost(real t, real x, real a, real b, real c) {
    return x+c;
}
native function real crossing_cost(real t, real x, real a, real b, real c) {
    return b*x+c*t*t;
}
record fixed = hybridadjoint(flow,section,reset,final_cost,0.0,
    options(parameters=options(a=2.0,b=1.0,c=.3),t1=1.0,sample_step=.05,
            event_direction=1,running_cost=running_cost,event_cost=crossing_cost));
display("Fixed-time objective, expected 5.85125: ",fixed.adjoint.objective);
display("Parameter gradient, expected [2.41375,2.4725,2.875]: ",
        fixed.adjoint.parameter_gradient);
record stopped = hybridadjoint(flow,section,reset,final_cost,0.0,
    options(parameters=options(a=2.0,b=1.0,c=.3),t1=1.0,sample_step=.05,
            event_direction=1,event_terminal=1,
            running_cost=running_cost,event_cost=crossing_cost));
display("Actual stopping time, expected 0.5: ",stopped.adjoint.terminal_time);
display("Moving-stop objective, expected 2.72625: ",stopped.adjoint.objective);
display("Parameter gradient, expected [-0.61125,4.6225,1.625]: ",
        stopped.adjoint.parameter_gradient);
display("Initial gradient, expected -0.9725: ",stopped.adjoint.initial_gradient);
display("Running and event costs: ",stopped.adjoint.running_cost,
        stopped.adjoint.event_cost);
% Each callback body is compiled once. Changed values/settings reuse the DLL/SO.
