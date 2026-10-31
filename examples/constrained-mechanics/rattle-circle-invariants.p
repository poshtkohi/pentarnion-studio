% Constant-mass separable mechanics with compiler-generated constraint jets.
clc;clear;close("all");
native function real zero_potential(real x,real y){return 0;}
native function real circle_constraint(real x,real y,real radius){
    return x*x+y*y-radius*radius;
}
record motion=constrainedmechanics(@zero_potential,@circle_constraint,[1,0],[0,1],
    options(constraint_parameters=options(radius=1),method="rattle",
      t1=6,time_step=0.01,sample_step=0.02,tolerance=1e-12));
display("Native derivative method: ",motion.constraint_derivatives);
display("Maximum scaled position/tangent residual: ",motion.max_position_error,motion.max_velocity_error);
display("Numerical relative energy drift: ",motion.max_relative_energy_drift);
display("Analytic centripetal multiplier is 1/2: ",motion.lambda(1),motion.lambda(end));
figure(1,"Name","RATTLE circle and invariants","NumberTitle","off");
subplot(1,2,1);plot(motion.q(:,1),motion.q(:,2));axis("equal");title("Constrained unit-circle motion");xlabel("$x$");ylabel("$y$");
subplot(1,2,2);plot(motion.t,motion.energy);title("Kinetic energy");xlabel("$t$");ylabel("$E$");
% method="shake" retains the same constrained positions while explicitly
% reporting unprojected endpoint velocities and their nonzero tangent residual.
