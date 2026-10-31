clc;
clear;
native function real kinetic(real mass,matrix<2,1> velocity) {
    return 0.5*mass*norm(velocity)^2;
}
record energy_units = unitcheck(kinetic,options(mass="kg",velocity="m/s"));
assert(energy_units.result_dimensions == [2,1,-2,0,0,0,0]);
display("Kinetic energy dimensions and native Jacobian dimensions:",energy_units);

ode oscillator(t,real x,real v;real omega) {
    x' = v;
    v' = -omega^2*x;
}
record annotations = options(t="s",x="m",v="m/s",omega="s^-1");
display("ODE dimensional verification:",unitcheck(oscillator,annotations));
solution result = odesolve(oscillator,x=1,v=0,omega=2,
    t0=0,t1=2,sample_step=0.025,units=annotations);
figure(1,"Name","Dimensionally checked oscillator","NumberTitle","off");
plot(result.t,result.x);xlabel("$t$ (s)");ylabel("$x$ (m)");
