clc; clear; close("all");
algebra H=builtin_algebra("quaternion");
ode node(t,element<H> q){q'=0*q;}
record network=graph([1,2]);
matrix<H,2,1> initial=[0;H.j];
record left_flow=networksolve(node,network,initial,options(coupling="general",
    edge_left=H.i,t1=2,step=0.002,sample_step=0.01,threads=0));
record right_flow=networksolve(node,network,initial,options(coupling="general",
    edge_right=H.i,t1=2,step=0.002,sample_step=0.01,threads=0));
matrix left_k=networkcoeff(left_flow,1,4);
matrix right_k=networkcoeff(right_flow,1,4);
assert(norm(left_k+right_k,"fro")<1e-10);
figure(1,"Name","Ordered quaternion edge operators","NumberTitle","off");
plot(left_flow.t,left_k(:,1));hold("on");plot(right_flow.t,right_k(:,1));hold("off");legend("Left i action","Right i action");
xlabel("$t$");ylabel("$k$ coefficient at node1");
display("Opposite rotations preserve noncommutative edge order:",left_flow.edge_grouping,right_flow.edge_grouping);
