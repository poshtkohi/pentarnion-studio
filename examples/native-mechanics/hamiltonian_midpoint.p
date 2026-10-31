% The second input is canonical momentum, explicitly distinct from velocity.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
native function real energy(real t,real q,real p,real mass,real stiffness,real coupling){return .5*(p*p/mass+stiffness*q*q)+coupling*q*p;}
record motion=mechanics(energy,1.0,.3,options(form="hamiltonian",parameters=options(mass=2.0,stiffness=1.0,coupling=.2),method="midpoint",t1=10.0,time_step=.02,sample_step=.02,newton_tolerance=1e-12));
display("Second input / native Newton Jacobian:",motion.second_input,motion.native_jacobian);
display("Maximum sampled relative Hamiltonian drift:",motion.max_relative_energy_drift);
figure(50,"Name","Canonical dynamics with native implicit midpoint","NumberTitle","off");
subplot(1,2,1);plot(motion.q(:,1),motion.p(:,1));title("Canonical phase curve");xlabel("q");ylabel("p");
subplot(1,2,2);plot(motion.t,motion.energy);title("Hamiltonian");xlabel("t");ylabel("H");
