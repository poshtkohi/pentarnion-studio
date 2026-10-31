% Real heat equation: centered finite differences and P1 finite elements.
use_algebra("A_gamma",gamma=1);
clc; clear; close("all");
real pi=3.141592653589793;
native function real no_reaction(real u,real t,real s){return 0.0;}
matrix x=linspace(0,1,33);
matrix u0=sin(pi*x);
record fd=pdesolve(no_reaction,x,u0,options(method="fd",boundary="dirichlet",diffusion=.1,t1=.5,time_step=.0005,sample_step=.025));
record fe=pdesolve(no_reaction,x,u0,options(method="fem",boundary="dirichlet",diffusion=.1,t1=.5,time_step=.0005,sample_step=.025));
matrix exact=exp(-.1*pi*pi*.5)*u0;
display("FD error norm:",norm(fd.values(end,:)-exact,2));
display("P1 FE error norm:",norm(fe.values(end,:)-exact,2));
assert(norm(fd.values(end,:)-exact,2)<.002);
assert(norm(fe.values(end,:)-exact,2)<.002);
figure(15,"Name","Real heat equation: FD and P1 FE","NumberTitle","off");
subplot(2,2,1);plot(x,fd.values(end,:));title("Finite difference: t=0.5");xlabel("Space");ylabel("u");
subplot(2,2,2);plot(x,fe.values(end,:));title("P1 finite element: t=0.5");xlabel("Space");ylabel("u");
subplot(2,2,3);imagesc(x,fd.t,fd.values);title("Finite-difference space-time field");xlabel("Space");ylabel("Time");
subplot(2,2,4);surf(x,fe.t,fe.values);title("Finite-element space-time surface");xlabel("Space");ylabel("Time");zlabel("u");
