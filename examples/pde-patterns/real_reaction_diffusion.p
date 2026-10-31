% Two-dimensional real logistic reaction-diffusion on a rectangular grid.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
matrix x=linspace(-1,1,41);matrix y=linspace(-1,1,33);
[matrix X,matrix Y]=meshgrid(x,y);
matrix initial=.8*exp(-12*(X.*X+Y.*Y));
native function real logistic(real u,real t,real x,real y,real rate){return rate*u*(1-u);}
record field=pdesolve2(logistic,x,y,initial,options(parameters=options(rate=2.0),integrator="imex_euler",boundary="neumann",diffusion_x=.02,diffusion_y=.05,t1=1.0,time_step=.01,sample_step=.1,threads=4));
display("2D grid / retained times:",field.nx,field.ny,size(field.t,1));
display("Requested / granted / used workers:",field.threads_requested,field.threads_granted,field.threads_used);
figure(42,"Name","Two-dimensional reaction-diffusion","NumberTitle","off");
subplot(1,2,1);imagesc(field.x,field.y,pdeslice(field,1));title("Initial field");xlabel("x");ylabel("y");
subplot(1,2,2);surf(field.x,field.y,pdeslice(field,size(field.t,1)));title("Final field");xlabel("x");ylabel("y");zlabel("u");
