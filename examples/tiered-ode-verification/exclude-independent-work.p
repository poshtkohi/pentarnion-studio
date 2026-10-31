use_algebra("A_gamma",gamma=1);
clc;clear;
% W is an auxiliary integral. It grows forever and must not enter recurrence.
% Neither physical equation depends on W; the compiler checks this explicitly.
ode cycle_with_work(t,real x,real y,real W) {
    x'=(1-x*x-y*y)*x-y;
    y'=x+(1-x*x-y*y)*y;
    W'=1.0;
}
record screen=discover(cycle_with_work,[1,0,0],
    options(t1=7.0,sample_step=.02,physical_coordinates=2,
        return_tolerance=.01,min_return_time=1.0));
record evidence=verifyexperiment(cycle_with_work,screen,[1],
    options(horizons=[20,40],refine_periodic=1));
display("Full final states including W: ",evidence.states);
display("Physical dimension / periodic closure: ",
    evidence.physical_coordinates,evidence.closure_residuals);
display("Distinct checked diagnostic kernel: ",evidence.physical_cache_key);
display(evidence.labels);
