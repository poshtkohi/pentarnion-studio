% Run natively in Studio, or explicitly export the BDF/scientific target adapters.
ode portable_flight(t,real height,real velocity;real gravity,real restitution) {
    height'=velocity;velocity'=-gravity;
}
native function real portable_ground(real t,real height,real velocity,real gravity,real restitution) { return height; }
native function [real next_height,real next_velocity] portable_bounce(real t,real height,real velocity,real gravity,real restitution) { return 0.0,-restitution*velocity; }
native function real portable_terminal(real t,real height,real velocity,real gravity,real restitution) { return .5*velocity*velocity+.3*height+.2*t; }
native function real portable_running(real t,real height,real velocity,real gravity,real restitution) { return .2*(height*height+velocity*velocity); }
native function real portable_impact_cost(real t,real height,real velocity,real gravity,real restitution) { return .1*velocity*velocity+.05*t; }
record trajectory=hybridsolve(portable_flight,portable_ground,portable_bounce,[1,0],options(parameters=options(gravity=9.81,restitution=.8),t1=1.,sample_step=.05,rtol=1e-10,atol=1e-12,event_direction=-1));
display("Impact time and complete saltation:",trajectory.event_times,trajectory.saltation);
display("Fixed-time parameter/initial sensitivity coordinates:",trajectory.sensitivities(end,:));
record reverse=hybridadjoint(portable_flight,portable_ground,portable_bounce,portable_terminal,[1,0],options(parameters=options(gravity=9.81,restitution=.8),t1=1.,sample_step=.2,rtol=1e-10,atol=1e-12,event_direction=-1,event_terminal=1,running_cost=portable_running,event_cost=portable_impact_cost));
display("Moving-terminal total objective:",reverse.adjoint.objective);
display("Reverse gravity/restitution gradients:",reverse.adjoint.parameter_gradient);
display("Actual reverse integration table:",reverse.adjoint.reverse_table);
figure(1);plot(trajectory.t,trajectory.values(:,1));title("Bouncing trajectory with explicit reset sides");
