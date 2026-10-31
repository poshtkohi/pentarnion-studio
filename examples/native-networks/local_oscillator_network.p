use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");
rng(2026);

ode local_oscillator(t,real x,real y;real mu){
    x'=(mu-x*x-y*y)*x-y;
    y'=x+(mu-x*x-y*y)*y;
}
record G=graphgen("cycle",12);
matrix initial=randn(12,2);
record s=networksolve(local_oscillator,G,initial,
    options(parameters=options(mu=1),t1=5,step=0.01,sample_step=0.05,
        diffusion=[0.2,0.2],threads=0));
display("One compiled local body for all twelve nodes:",s.cache_key);
display("Native work and observed threads:",s.native_work,s.threads_used);

figure(1,"Name","A coupled oscillator experiment","NumberTitle","off");
subplot(1,2,1);plot(networkcoeff(s,1,1),networkcoeff(s,1,2));
hold("on");plot(networkcoeff(s,7,1),networkcoeff(s,7,2));hold("off");
title("Phase portraits of two selected nodes");xlabel("$x$");ylabel("$y$");
legend("node 1","node 7");
subplot(1,2,2);plot(s.t,s.synchronization);
title("Measured network synchronization error");xlabel("$t$");
caption("A finite-time experiment; convergence is a numerical observation, not a universal claim.");
