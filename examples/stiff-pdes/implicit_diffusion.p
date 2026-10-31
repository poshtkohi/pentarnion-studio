% Implicit diffusion, explicit reaction: first-order IMEX Euler.
% This dt exceeds the explicit RK4 diffusion limit on this grid.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
real pi=3.141592653589793;
matrix x=linspace(0,1,65);
matrix initial=sin(pi*x);
native function real zero_reaction(real u,real t,real x){return 0.0;}
record fd=pdesolve(zero_reaction,x,initial,options(method="fd",integrator="imex_euler",boundary="dirichlet",diffusion=1.0,t1=.2,time_step=.01,sample_step=.01,threads=4));
record fe=pdesolve(zero_reaction,x,initial,options(method="fem",integrator="imex_euler",boundary="dirichlet",diffusion=1.0,t1=.2,time_step=.01,sample_step=.01,threads=4));
display("FD / P1 FE final central value:",fd.values(end,33),fe.values(end,33));
display("Continuum heat equation reference:",exp(-pi*pi*.2));
display("FD linear solves / factorizations:",fd.linear_solves,fd.factorizations);
figure(41,"Name","Implicit diffusion: FD and P1 FE","NumberTitle","off");
subplot(1,2,1);imagesc(fd.x,fd.t,fd.values);title("FD: explicit reaction, implicit diffusion");xlabel("x");ylabel("t");
subplot(1,2,2);imagesc(fe.x,fe.t,fe.values);title("P1 FE with consistent mass");xlabel("x");ylabel("t");
% Refine time_step for temporal accuracy; explicit reactions may impose their own bound.
