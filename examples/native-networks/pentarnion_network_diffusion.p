use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");

ode stationary_pentarnion_node(t,pentarnion p){p'=0*p;}
record G=graphgen("path",4);
matrix initial=[1+i;2*j;-1+k+ell;1-i+j-k+ell];
record s=networksolve(stationary_pentarnion_node,G,initial,
    options(t1=3,step=0.01,sample_step=0.05,diffusion=0.7,threads=0));
display("Every local typed state width:",s.state_widths);
display("Initial/final coefficient means:",s.means(1,:),s.means(end,:));
display("All native node coefficients at final time:",s.values(end,:));

figure(1,"Name","Pentarnion diffusion on a native graph","NumberTitle","off");
subplot(2,2,1);plotgraph(G,[0,0;1,0;2,0;3,0]);
title("Four stable graph nodes");
subplot(2,2,2);plot(s.t,networkcoeff(s,1,2));
hold("on");plot(s.t,networkcoeff(s,4,2));hold("off");
title("The $i$ coefficient at the end nodes");xlabel("$t$");ylabel("$p_i$");
subplot(2,2,3);plot(s.t,networkcoeff(s,1,5));
hold("on");plot(s.t,networkcoeff(s,4,5));hold("off");
title("The $\\ell$ coefficient at the end nodes");xlabel("$t$");ylabel("$p_\\ell$");
subplot(2,2,4);plot(s.t,s.synchronization);
title("All five coordinates enter the reported error");xlabel("$t$");
caption("Metric: Euclidean coefficient space, explicitly recorded in s.metric.");
