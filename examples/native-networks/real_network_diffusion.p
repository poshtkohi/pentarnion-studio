use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");

ode stationary_node(t,real x){x'=0*x;}
record G=graphgen("path",2);
record s=networksolve(stationary_node,G,[1;3],
    options(t1=2,step=0.01,sample_step=0.02,diffusion=0.7,threads=0));
matrix first=networkcoeff(s,1,1);
matrix second=networkcoeff(s,2,1);
assert(abs(first(end)-2+exp(-2.8))<1e-9);
assert(abs(second(end)-2-exp(-2.8))<1e-9);
display("Shared native body cache:",s.cache_key);
display("Threads used:",s.threads_used);
display("Initial/final real means:",s.means(1,:),s.means(end,:));

figure(1,"Name","Analytic two-node graph diffusion","NumberTitle","off");
subplot(1,2,1);plot(s.t,first);
hold("on");plot(s.t,second);hold("off");
title("The two nodes approach their conserved mean");xlabel("$t$");ylabel("$x$");
legend("node 1","node 2");
subplot(1,2,2);plot(s.t,s.synchronization);
title("Euclidean synchronization error");xlabel("$t$");ylabel("$E(t)$");
caption("Independent reference: E(t)=exp(-1.4t). Native fixed-step RK4.");
