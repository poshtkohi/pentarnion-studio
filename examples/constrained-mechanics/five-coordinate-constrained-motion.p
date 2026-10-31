% Five coefficient coordinates are explicitly interpreted as real configuration.
use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
native function real coefficient_potential(real a,real b,real c,real d,real e){return 0;}
native function [real sphere,real third,real fourth,real fifth] coefficient_constraints(real a,real b,real c,real d,real e){
    return a*a+b*b-1,c,d,e;
}
pentarnion q0=1;
pentarnion v0=i;
record motion=constrainedmechanics(@coefficient_potential,@coefficient_constraints,q0,v0,
    options(t1=2,time_step=0.01,sample_step=0.02,tolerance=1e-12));
display("Explicit real configuration dimension: ",motion.coordinate_dimension);
display("Independent real constraints: ",motion.constraint_count);
display("All five coordinates: ",motion.q(end,:));
display("Tangent velocity error: ",motion.max_velocity_error);
figure(1,"Name","Five-coordinate constrained configuration","NumberTitle","off");
plot(motion.q(:,1),motion.q(:,2));axis("equal");xlabel("$1$");ylabel("$i$");title("Explicit circle in the first two coefficient coordinates");
% Projection is opt-in and its corrections are retained in the returned record:
record corrected=constrainedmechanics(@coefficient_potential,@coefficient_constraints,
    [1.01,0,0,0,0],[0.1,1,0,0,0],
    options(initial_policy="project",t1=0.1,time_step=0.01,sample_step=0.01));
display("Initial position correction: ",corrected.initial_q_correction);
display("Initial velocity correction: ",corrected.initial_v_correction);
