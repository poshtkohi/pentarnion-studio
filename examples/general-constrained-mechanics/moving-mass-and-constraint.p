clc;
clear;
native function real L(real t,real q,real v) {
    return 0.5*(1+q*q)*v*v;
}
native function real moving_constraint(real t,real q) {
    return q-t;
}
record result = mechanicsdae(L,moving_constraint,0,1,
    options(t1=1,sample_step=0.01,rtol=1e-9,atol=1e-11));
display("Moving constraint with nonlinear mass M(q)=1+q^2:",result);
figure(1,"Name","Moving constraint and state-dependent mass","NumberTitle","off");
subplot(2,2,1);plot(result.t,result.q);title("$q=t$");
subplot(2,2,2);plot(result.t,result.lambda);title("Reaction $\\lambda=t$");
subplot(2,2,3);plot(result.t,result.energy);title("Energy $E=(1+t^2)/2$");
subplot(2,2,4);plot(result.t,result.energy_balance);title("Energy minus supplied work");
