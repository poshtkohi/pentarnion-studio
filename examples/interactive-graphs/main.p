use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

% Edge IDs are input rows; node IDs remain unchanged when a node is moved.
record G = digraph([1,2; 1,3; 2,3; 2,4; 3,4],
    options(weights=[2,7,1,6,2],
        labels=options(n1="Start",n2="Junction",n3="Bridge",n4="Goal")));
record route = shortestpath(G,1,4);
display("Shortest path and its physical edge IDs:",route);
assert(route.distance==5);

figure(1,"Name","Shortest path with stable node identities","NumberTitle","off");
plotgraph(G,[0,0; 1,1; 1,-1; 2,0],
    options(path=route.path,highlight_edges=route.edge_ids,edge_labels=1));
title("A directed weighted shortest path");
caption("Red marks the route; Ctrl selects multiple nodes/edges. Use Move node to drag.");

% Numerical graph data is immutable; dragging edits this plot's positions only.
display("Original graph Laplacian:",graphlaplacian(G));
