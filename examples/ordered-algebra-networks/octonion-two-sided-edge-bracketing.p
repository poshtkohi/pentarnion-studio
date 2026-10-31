clc; clear; close("all");
algebra O=builtin_algebra("octonion");
ode node(t,element<O> q){q'=0*q;}
record network=graph([1,2]);matrix<O,2,1> initial=[0;O.e2];
record left_grouped=networksolve(node,network,initial,options(coupling="general",
    edge_left=O.e1,edge_right=O.e4,edge_grouping="(left*delta)*right",
    t1=0.5,step=0.001,sample_step=0.005));
record right_grouped=networksolve(node,network,initial,options(coupling="general",
    edge_left=O.e1,edge_right=O.e4,edge_grouping="left*(delta*right)",
    t1=0.5,step=0.001,sample_step=0.005));
assert(norm(left_grouped.values-right_grouped.values,"fro")>0.01);
matrix ordered_difference=left_grouped.values-right_grouped.values;
figure(1,"Name","Nonassociative network grouping","NumberTitle","off");
plot(left_grouped.t,ordered_difference(:,1));hold("on");plot(left_grouped.t,ordered_difference(:,5));hold("off");
legend("Coordinate1 difference","Coordinate5 difference");
display("Bracketing changes the native network trajectory; complete8-coordinate nodes are retained.");
