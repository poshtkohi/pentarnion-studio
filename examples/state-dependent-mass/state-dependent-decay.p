clc;
clear;
native function real decay_force(real t,real y) {
    return -y-y*y;
}
native function matrix<1,1> state_mass(real t,real y) {
    return [1+y];
}
record result = masssolve(decay_force,state_mass,1,-1,
    options(t1=5,sample_step=0.025,rtol=1e-10,atol=1e-12));
display("Native state-dependent mass solve:",result);
figure(1,"Name","State-dependent mass: exact decay","NumberTitle","off");
plot(result.t,result.values);
title("$(1+y)y'=-(y+y^2)$, $y(0)=1$");xlabel("$t$");ylabel("$y$");
