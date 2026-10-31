% Consistent-mass high-order FEM with compiler-generated nonlinear Jacobians.
native function [real x,real y] plane(real u,real v){return u,v;}
native function real cubic(real u,real t,real x,real y,real rate){return rate*u*u*u;}
record mesh=paramesh(plane,linspace(0,1,3),linspace(0,1,3));
record simulation=femtime(cubic,mesh,options(parameters=options(rate=1000.0),initial=1.0,
    diffusion=0.0,order=3,t0=0.0,t1=.02,time_step=.01,sample_step=.002,
    rtol=.001,atol=1e-8,threads=0));
matrix values=femtimecoeff(simulation,0);
assert(simulation.converged);
assert(abs(values(end,1)-1/sqrt(41))<.004);
display("Accepted/rejected steps: ",simulation.accepted_steps,simulation.rejected_steps);
figure(1,"Name","Native stiff cubic: consistent-mass H1 FEM","NumberTitle","off");
plot(simulation.t,values(:,1));xlabel("$t$");ylabel("$u(t)$");
title("Adaptive implicit FEM, with zero diffusion");
figure(2,"Name","Retained complete H1 field","NumberTitle","off");
record final_field=femtimeslice(simulation,length(simulation.t));
femsystemplot(final_field);title("Full high-order field at the final sample");
