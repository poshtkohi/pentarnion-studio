% A genuine index-one system: y'=-y+z, 0=z-sin(t).
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
native function [real differential,real constraint] residual(real t,real y,real z,real dy,real dz){return dy+y-z,z-sin(t);}
% Keep differential y(0)=0 and algebraic derivative dz(0)=1 fixed.
% Ask IDAS to correct algebraic z(0) and differential dy(0).
record trajectory=daesolve(residual,[0,.3],[.7,1],options(algebraic=[0,1],initialization="consistent",jacobian="analytic",t1=2.0,sample_step=.02,rtol=1e-9,atol=1e-11));
display("Consistent y and y' initials:",trajectory.consistent_initial,trajectory.consistent_derivative);
display("Initial correction / largest sampled residual:",trajectory.initial_correction_norm,trajectory.maximum_sample_residual);
matrix reference=.5*(sin(trajectory.t)-cos(trajectory.t)+exp(-trajectory.t));
assert(norm(trajectory.values(:,1)-reference,2)<1e-6);
figure(46,"Name","Native index-one differential-algebraic trajectory","NumberTitle","off");
subplot(1,2,1);plot(trajectory.t,trajectory.values(:,1));title("Differential coordinate");xlabel("t");ylabel("y");
subplot(1,2,2);plot(trajectory.t,trajectory.values(:,2));title("Algebraic constraint: z=sin(t)");xlabel("t");ylabel("z");
