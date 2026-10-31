use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

% An undirected three-node path with real diffusion weights. Each node retains
% all five Pentarnion coefficients. This example has no implicit product of
% Pentarnion weights: multiplying the real kappa is unambiguous.
record network = graph([1,2; 2,3]);
matrix L = graphlaplacian(network);
display("Path Laplacian:",L);

% This explicitly declared network RHS and its analytic Jacobian are compiled
% to a cached native DLL/SO by the existing ODE compiler.
ode diffusion(t, pentarnion p1, pentarnion p2, pentarnion p3; real kappa) {
    p1' = kappa*(p2-p1);
    p2' = kappa*(p1-2*p2+p3);
    p3' = kappa*(p2-p3);
}
pentarnion a = 1+i;
pentarnion b = 2*j;
pentarnion c = -1+k+ell;
pentarnion network_mean = (a+b+c)/3;
solution s = odesolve(diffusion,p1=a,p2=b,p3=c,kappa=0.7,
    t0=0,t1=12,sample_step=0.04,method="bdf",jacobian="analytic",
    rtol=1e-10,atol=1e-12);
assert(norm2(s.p1(end)+s.p2(end)+s.p3(end)-3*network_mean)<1e-16);
assert(norm2(s.p1(end)-network_mean)<0.000001);
assert(norm2(s.p2(end)-network_mean)<0.000001);
assert(norm2(s.p3(end)-network_mean)<0.000001);
display("Conserved network mean:",network_mean);
display("Final native node states:",s.p1(end),s.p2(end),s.p3(end));

figure(1,"Name","Pentarnion diffusion on a graph","NumberTitle","off");
subplot(2,2,1);plotgraph(network,[0,0; 1,0; 2,0]);
title("Three-node diffusion network");
subplot(2,2,2);plot(s.t,coeff(s.p1,1));
title("Node1: coefficient of $i$");xlabel("$t$");ylabel("$p_{1,i}$");
subplot(2,2,3);plot(s.t,coeff(s.p2,1));
title("Node2: coefficient of $i$");xlabel("$t$");ylabel("$p_{2,i}$");
subplot(2,2,4);plot(s.t,coeff(s.p3,1));
title("Node3: coefficient of $i$");xlabel("$t$");ylabel("$p_{3,i}$");
caption("Real diffusive coupling conserves the complete Pentarnion mean.");
