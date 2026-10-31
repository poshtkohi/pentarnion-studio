use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");
ode decay(t,pentarnion p;real rate) {
    p' = -rate*p;
}
pentarnion initial = 1+i+j+k+ell;
% All solves reuse the same compiled equation and generated Jacobian.
solution explicit_run = odesolve(decay,p=initial,rate=0.7,
    t0=0,t1=10,sample_step=0.1,method="erk",rtol=1e-8,atol=1e-10);
solution implicit_run = odesolve(decay,p=initial,rate=0.7,
    t0=0,t1=10,sample_step=0.1,method="esdirk",jacobian="analytic",rtol=1e-8,atol=1e-10);
display("Dormand-Prince 5(4):",explicit_run);
display("ESDIRK 4(3):",implicit_run);
figure(1,"Name","Native ODE methods","NumberTitle","off");
subplot(1,2,1);
plot(explicit_run.t,coeff(explicit_run.p,0));
title("Explicit RK: Dormand-Prince 5(4)");
xlabel("$t$"); ylabel("$p_0(t)$");
subplot(1,2,2);
plot(implicit_run.t,coeff(implicit_run.p,0));
title("ESDIRK 4(3)");
xlabel("$t$"); ylabel("$p_0(t)$");
