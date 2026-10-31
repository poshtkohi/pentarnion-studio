% All five real configuration coordinates are retained; product grouping is explicit.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
pentarnion A=1+.1*i;
pentarnion B=.05*j;
native function pentarnion force(real t,pentarnion q,pentarnion A,pentarnion B,real eta){return -(A*q)+(1-eta)*(B*(q*q))+eta*((B*q)*q);}
record motion=mechanics(force,.1+.2*i-.1*j+.05*k+.1*ell,0*i,options(form="newton",parameters=options(A=A,B=B,eta=0.0),force_layout="position",mass=1.0,method="verlet",t1=2.0,time_step=.002,sample_step=.02));
display("Final configuration coefficients (1,i,j,k,ell):",motion.q(end,:));
display("Kinetic energy is a defined Euclidean diagnostic, not an inferred algebra norm:",motion.kinetic_energy(end));
figure(51,"Name","Ordered Pentarnion configuration dynamics","NumberTitle","off");
subplot(1,2,1);plot(motion.t,motion.q(:,1));title("Scalar configuration coefficient");xlabel("t");ylabel("q0");
subplot(1,2,2);plot3(motion.q(:,2),motion.q(:,3),motion.q(:,4));title("Projection of configuration trajectory");xlabel("i");ylabel("j");zlabel("k");
