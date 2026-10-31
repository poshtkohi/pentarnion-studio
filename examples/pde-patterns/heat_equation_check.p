% Headless-capable manufactured heat mode; no figure operations.
use_algebra("A_gamma",gamma=1);clc;clear;
real pi=3.141592653589793;
matrix x=linspace(0,1,17);matrix y=linspace(0,1,17);
[matrix X,matrix Y]=meshgrid(x,y);
matrix initial=sin(pi*X).*sin(pi*Y);
native function real heat(real u,real t,real x,real y){return 0.0;}
record field=pdesolve2(heat,x,y,initial,options(integrator="imex_euler",boundary="dirichlet",diffusion=.1,t1=.1,time_step=.0005,sample_step=.1,threads=4));
matrix final=pdeslice(field,2);
matrix errors=abs(final-exp(-.2*pi*pi*.1)*initial);
matrix largest=max(errors(:));
real max_error=largest(1);
display("Heat equation max coefficient error:",max_error);
assert(max_error<.002);
