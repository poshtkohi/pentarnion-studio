% Every sample retains all five coefficients; the coefficient box inspects them.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
real pi=3.141592653589793;
matrix x=(0:15)/16;matrix y=(0:11)/12;
[matrix X,matrix Y]=meshgrid(x,y);
matrix initial=.1*sin(2*pi*X)+.2*cos(2*pi*Y)*i+.15*sin(2*pi*(X+Y))*j+.1*cos(2*pi*X)*k+.12*sin(2*pi*Y)*ell;
pentarnion A=1+i+j+ell;
native function pentarnion reaction(pentarnion p,real t,real x,real y,pentarnion A){return -.2*p+.05*(A*p-p*A);}
record field=pdesolve2(reaction,x,y,initial,
    options(parameters=options(A=A),integrator="imex_euler",boundary="periodic",
        length_x=1.0,length_y=1.0,diffusion=.01,t1=.5,time_step=.005,sample_step=.05,threads=4));
figure(1,"Name","Pentarnion field: all coefficients retained","NumberTitle","off");
animatefield(field,options(mode="image",component=1,fps=10.0,loop=1));
title("Pentarnion reaction-diffusion coefficients");xlabel("$x$");ylabel("$y$");
animationframe(3);
display("The frame slider and coefficient selector use the original full field.");
display("Underlying native callback reused:",field.cache_hit);
