% A pendulum in Cartesian coordinates, with no hand-written multipliers/Jacobian.
clc;clear;close("all");
native function real gravitational_energy(real x,real y,real gravity){return gravity*y;}
native function real rod_constraint(real x,real y,real rod_length){return x*x+y*y-rod_length*rod_length;}
real angle0=0.6;
real speed0=0.1;
record motion=constrainedmechanics(@gravitational_energy,@rod_constraint,
    [sin(angle0),-cos(angle0)],speed0*[cos(angle0),sin(angle0)],
    options(parameters=options(gravity=9.81),constraint_parameters=options(rod_length=1),
      mass=1,method="rattle",t1=5,time_step=0.01,sample_step=0.02,tolerance=1e-12));
display("Position / velocity residuals: ",motion.max_position_error,motion.max_velocity_error);
display("Maximum numerical energy drift: ",motion.max_relative_energy_drift);
display("Potential and constraint native cache keys: ",motion.potential_cache_key,motion.constraint_cache_key);
figure(1,"Name","Native constrained pendulum","NumberTitle","off");
subplot(2,2,1);plot(motion.q(:,1),motion.q(:,2));axis("equal");title("Cartesian pendulum trajectory");xlabel("$x$");ylabel("$y$");
subplot(2,2,2);plot(motion.t,motion.energy);title("Numerical total energy");xlabel("$t$");ylabel("$E$");
subplot(2,2,3);plot(motion.t,motion.lambda(:,1));title("Generated physical multiplier");xlabel("$t$");ylabel("$\\lambda$");
subplot(2,2,4);plot(motion.t,motion.position_error);title("Scaled holonomic position residual");xlabel("$t$");ylabel("Residual");
