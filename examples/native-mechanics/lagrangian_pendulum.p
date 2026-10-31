% Compiler AD2 supplies Lq, Lv, Lvv, Lvq and Lvt; no handwritten equations.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
native function real lagrangian(real t,real angle,real velocity,real mass,real rod_length,real gravity){return .5*mass*rod_length*rod_length*velocity*velocity+mass*gravity*rod_length*cos(angle);}
native function real damping(real t,real angle,real velocity,real coefficient){return -coefficient*velocity;}
% This demonstration fits Studio's default operation budget; refine the step
% and compare trajectories when using it for quantitative research.
record motion=mechanics(lagrangian,.8,0.0,options(form="lagrangian",parameters=options(mass=1.0,rod_length=1.0,gravity=1.0),force=damping,force_parameters=options(coefficient=.05),method="rk4",t1=5.0,time_step=.01,sample_step=.02));
display("Initial/final defined Lagrangian energy:",motion.energy(1),motion.energy(end));
display("Model/generalized force evaluations:",motion.evaluations,motion.force_evaluations);
assert(motion.energy(end)<motion.energy(1));
figure(49,"Name","Native Euler-Lagrange pendulum","NumberTitle","off");
subplot(1,2,1);plot(motion.t,motion.q(:,1));title("Damped pendulum angle");xlabel("t");ylabel("Angle");
subplot(1,2,2);plot(motion.q(:,1),motion.p(:,1));title("Coordinate and generalized momentum");xlabel("Angle");ylabel("Momentum");
