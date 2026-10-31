use_algebra("A_gamma",gamma=1);
clc;clear;
ode flight(t,real height,real velocity;real gravity,real restitution) {
    height'=velocity;velocity'=-gravity;
}
native function real ground(real t,real height,real velocity,real gravity,real restitution) {
    return height;
}
native function [real height,real velocity] bounce(real t,real height,real velocity,real gravity,real restitution) {
    return 0.0,-restitution*velocity;
}
record event=hybridsolve(flight,ground,bounce,[1,0],
    options(parameters=options(gravity=1.0,restitution=.8),t1=2.2,
        event_direction=-1,event_terminal=1));
display("Moving terminal event time: ",event.t(end));
display("Terminal time derivatives: ",event.terminal_time_derivatives);
display("State derivatives AT THE MOVING EVENT: ",event.terminal_value_derivatives);
display("Different fixed-time post-reset sensitivities: ",event.sensitivities(end,:));
% The initial-velocity terminal derivative is zero here, even though its
% fixed-time post-reset sensitivity is nonzero. They are different quantities.
