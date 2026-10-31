use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
ode flight(t,real height,real velocity;real gravity,real restitution) {
    height'=velocity;
    velocity'=-gravity;
}
native function real ground(real t,real height,real velocity,real gravity,real restitution) {
    return height;
}
native function [real height,real velocity] bounce(real t,real height,real velocity,real gravity,real restitution) {
    return 0.0,-restitution*velocity;
}
record trajectory=hybridsolve(flight,ground,bounce,[1,0],
    options(parameters=options(gravity=1.0,restitution=.8),t1=2.2,
        sample_step=.025,rtol=1e-9,atol=1e-11,event_direction=-1));
display("Localized impact time; expected sqrt(2): ",trajectory.event_times);
display("Event-time derivatives [gravity,restitution,h0,v0]: ",
    trajectory.event_time_derivatives);
display("State saltation matrix, flattened by rows: ",trajectory.saltation);
display("Final fixed-time sensitivities: ",trajectory.sensitivities(end,:));
display("Exact pre/post reset states: ",
    hybridval(trajectory,trajectory.event_times(1),"pre"),
    hybridval(trajectory,trajectory.event_times(1),"post"));

% Draw each continuous epoch separately; never connect phase states across a jump.
figure(1,"Name","Native hybrid impact epochs","NumberTitle","off");
matrix selected=[1];
subplot(1,2,1);
for(epoch=1:trajectory.segments) {
    selected=find((trajectory.segment_ids>=epoch)&(trajectory.segment_ids<=epoch));
    plot(trajectory.t(selected),trajectory.values(selected,1));hold("on");
}
title("Height by continuous epoch");xlabel("t");ylabel("height");
subplot(1,2,2);
for(epoch=1:trajectory.segments) {
    selected=find((trajectory.segment_ids>=epoch)&(trajectory.segment_ids<=epoch));
    plot(trajectory.values(selected,1),trajectory.values(selected,2));hold("on");
}
title("Phase trajectory with a reset discontinuity");xlabel("height");ylabel("velocity");
