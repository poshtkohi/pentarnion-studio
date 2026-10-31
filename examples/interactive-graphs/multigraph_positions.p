use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

record G = digraph([1,2; 1,2; 2,1; 2,2; 2,3],
    options(n=4,multigraph=1,weights=[5,1,3,2,4],
        labels=options(n1="Source",n2="Loop",n3="Target",n4="Isolated")));

figure(1,"Name","Parallel edges and self loops","NumberTitle","off");
subplot(1,2,1);
matrix planar = graphlayout(G,options(layout="grid",seed=7));
plotgraph(G,planar,options(highlight_edges=[2,5],highlight_nodes=[1,2,3],edge_labels=1));
title("Distinct physical edges retain their input row IDs");

subplot(1,2,2);
matrix spatial = graphlayout(G,options(layout="sphere",dimensions=3,radius=2));
plotgraph(G,spatial,options(selected_nodes=[2],selected_edges=[4]));
title("A reproducible three dimensional graph view");
caption("Orange denotes selection; drag in the current camera plane, or rotate the view.");
