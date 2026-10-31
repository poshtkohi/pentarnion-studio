% A planar central force with explicitly position-only compiled callback.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
real pi=3.141592653589793;
native function [real fx,real fy] gravity(real t,real x,real y,real GM){real r=sqrt(x*x+y*y);return -GM*x/(r^3),-GM*y/(r^3);}
record orbit=mechanics(gravity,[1,0],[0,1],options(form="newton",force_layout="position",parameters=options(GM=1.0),mass=1.0,method="verlet",t1=2*pi,time_step=.005,sample_step=.02));
matrix radius=sqrt(orbit.q(:,1).*orbit.q(:,1)+orbit.q(:,2).*orbit.q(:,2));
matrix physical_energy=orbit.kinetic_energy-1./radius;
display("Initial/final separately defined Kepler energy:",physical_energy(1),physical_energy(end));
figure(48,"Name","Newtonian central force with native Verlet","NumberTitle","off");
subplot(1,2,1);plot(orbit.q(:,1),orbit.q(:,2));title("Planar orbit");xlabel("x");ylabel("y");
subplot(1,2,2);plot(orbit.t,physical_energy);title("Numerical physical energy");xlabel("t");ylabel("$T-1/r$");
