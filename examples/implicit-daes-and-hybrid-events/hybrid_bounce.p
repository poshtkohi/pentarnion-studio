% Continuous root localization followed by a cached native reset and IDAS restart.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
native function [real kinematic,real dynamic] residual(real t,real height,real velocity,real dheight,real dvelocity,real gravity){return dheight-velocity,dvelocity+gravity;}
native function real ground(real t,real height,real velocity,real dheight,real dvelocity){return height;}
native function [real height,real velocity,real dheight,real dvelocity] bounce(real t,real h,real v,real dh,real dv,real gravity,real restitution){return 0.0,-restitution*v,-restitution*v,-gravity;}
record trajectory=daesolve(residual,[1,0],[0,-1],options(parameters=options(gravity=1.0),algebraic=[0,0],event=ground,event_direction=-1,reset=bounce,reset_parameters=options(gravity=1.0,restitution=.8),t1=5.0,sample_step=.01,max_events=10,minimum_event_interval=1e-7,rtol=1e-9,atol=1e-11));
display("Localized impact times:",trajectory.event_times);
display("States immediately before / after impact:",trajectory.event_before,trajectory.event_after);
assert(abs(trajectory.event_times(1)-sqrt(2))<1e-7);
figure(47,"Name","Cached native hybrid impact resets","NumberTitle","off");
subplot(1,2,1);plot(trajectory.t,trajectory.values(:,1));title("Height with localized impacts");xlabel("t");ylabel("Height");
subplot(1,2,2);plot(trajectory.t,trajectory.values(:,2));title("Velocity after native resets");xlabel("t");ylabel("Velocity");
% Sampled curves connect stored samples; event_before/after preserve exact jumps.
