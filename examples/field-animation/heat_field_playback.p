% Native 2D heat solve followed by playback of immutable saved samples.
clc; clear; close("all");
real pi=3.141592653589793;
matrix x=linspace(0,1,17);matrix y=linspace(0,1,17);
[matrix X,matrix Y]=meshgrid(x,y);
matrix initial=sin(pi*X).*sin(pi*Y);
native function real no_reaction(real u,real t,real x,real y){return 0.0;}
record field=pdesolve2(no_reaction,x,y,initial,
    options(integrator="imex_euler",boundary="dirichlet",diffusion=.1,
        t1=.2,time_step=.002,sample_step=.02,threads=4));
display("Saved frames and native spatial workers:",size(field.t,1),field.threads_used);
figure(1,"Name","Heat diffusion: saved field playback","NumberTitle","off");
animatefield(field,options(mode="surface",component=0,fps=10.0,loop=1));
title("Real heat diffusion");xlabel("$x$");ylabel("$y$");zlabel("$u(x,y,t)$");
% Use Play/Pause, Previous/Next or the frame slider in this plot's toolbar.
% These controls read saved samples; they never run the PDE solver on the GUI.
% playanimation("on");
