use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

% Node IDs are one-based. Edges and weights are stored as immutable records.
record G = graph([1,2; 2,3; 3,4; 4,1; 1,3],
    options(n=5, weights=[1,1,1,1,3],
        labels=options(n1="A", n2="B", n3="C", n4="D", n5="isolated")));
record info = graphinfo(G);
record connected = components(G);
record route = shortestpath(G,1,3);
record forest = spanningtree(G);
record importance = centrality(G,options(kind="betweenness",weighted=0));
matrix L = graphlaplacian(G);
matrix A = graphadjacency(G);
record spectrum = eig(L);
assert(connected.count==2);
assert(route.distance==2);
assert(forest.total_weight==3);
assert(forest.spanning==0);
assert(norm(sum(L,2))<1e-12);
display("Graph summary:",info);
display("Shortest route from A to C:",route);
display("Minimum spanning forest (node5 is isolated):",forest);
display("Betweenness using hop distances:",importance.scores);
display("Real graph Laplacian:",L);
display("Laplacian spectrum (two zero eigenvalues for two components):",spectrum.values_real);

figure(1,"Name","Graph paths and directed networks","NumberTitle","off");
subplot(1,2,1);
plotgraph(G);
title("Undirected graph with one isolated node");
xlabel("$x$"); ylabel("$y$");
caption("Data cursor reports the numeric node ID; names are in G.labels.");

% Negative edges are supported by Bellman-Ford; cycles with negative total
% weight are reported explicitly instead of returning a finite shortest path.
record D = digraph([1,2; 1,3; 2,3; 3,4],options(weights=[2,5,-1,2]));
record directed_route = shortestpath(D,1,4);
record order = toposort(D);
assert(directed_route.distance==3);
assert(directed_route.method=="bellman_ford");
assert(order.acyclic==1);
display("Directed shortest path:",directed_route);
display("Topological order:",order.order);
subplot(1,2,2);
plotgraph(D,[0,0; 1,1; 1,-1; 2,0]);
title("Directed acyclic graph; arrowheads show direction");
caption("The route 1 to 2 to 3 to 4 has cost3.");

record N = digraph([1,2; 2,3; 3,2],options(weights=[1,-2,1]));
record impossible_route = shortestpath(N,1,3);
assert(impossible_route.negative_cycle==1);
assert(impossible_route.distance_available==0);
display("Negative-cycle diagnostic:",impossible_route);

% Weighted degree/strength, SCCs, cycle witnesses and circle coordinates.
record strengths = centrality(G,options(kind="strength"));
record strongly_connected = graphcomponents(D,options(kind="strong"));
record cycle = graphcycle(G);
matrix xyz = graphlayout(G,options(dimensions=3,radius=2));
display("Node strengths:",strengths.scores);
display("Strong components of the DAG:",strongly_connected.labels);
display("One cycle, preserving original edge IDs:",cycle);
display("Reusable node positions:",xyz);
