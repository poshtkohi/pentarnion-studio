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
native function real terminal_energy(real t,real height,real velocity,real gravity,real restitution) {
    return .5*(height*height+velocity*velocity)+.3*restitution;
}
record result=hybridadjoint(flight,ground,bounce,terminal_energy,[1,0],
    options(parameters=options(gravity=1.0,restitution=.8),t1=2.2,
        sample_step=.05,event_direction=-1,max_steps=100000));
display("Fixed-time native objective: ",result.adjoint.objective);
display("True reverse-mode parameter gradient [gravity,restitution]: ",
    result.adjoint.parameter_gradient);
display("Initial-state gradient [height,velocity]: ",result.adjoint.initial_gradient);
display("Native backward steps/checkpoints: ",
    result.adjoint.backward_steps,result.adjoint.checkpoints);
display("Adjoint covectors before/after the saltation transpose: ",
    result.adjoint.event_before,result.adjoint.event_after);
% This is actual checkpointed CVODES reverse integration on each smooth epoch.
% Running/event costs and moving-terminal-event objectives are separate features.
