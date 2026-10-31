use_algebra("A_gamma",gamma=1);
clc;
clear;

ode growth(t,real y;real rate){ y'=rate*y; }
% Fixed terminal time. Objective returns one real scalar and compiles to a DLL/SO.
native function real terminal_energy(real time,real y,real rate){
    return 0.5*y*y+3.0*rate;
}
record a=odeadjoint(growth,2.0,options(parameters=options(rate=0.4),
    objective=terminal_energy,t0=0,t1=1.2,sample_step=0.2,method="bdf",
    rtol=1e-10,atol=1e-12));
real final_y=2*exp(0.4*1.2);
assert(abs(a.objective-(0.5*final_y*final_y+1.2))<1e-7);
assert(abs(a.parameter_gradient(1)-(1.2*final_y*final_y+3))<1e-5);
assert(abs(a.initial_gradient(1)-final_y*exp(0.4*1.2))<1e-5);
display("Terminal objective:",a.objective);
display("Checkpointed adjoint model-parameter gradient:",a.parameter_gradient);
display("Checkpointed adjoint initial-state gradient:",a.initial_gradient);
display("Backward steps and checkpoints:",a.backward_steps,a.checkpoints);
