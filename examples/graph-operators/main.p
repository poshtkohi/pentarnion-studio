use_algebra("A_gamma",gamma=1);
clc;
clear;

record G=graphgen("path",5,options(weight=2));
record A=graphsparse(G,options(kind="adjacency"));
record B=graphsparse(G,options(kind="incidence"));
record L=graphsparse(G,options(kind="laplacian"));
display("Canonical sparse weighted Laplacian:",L);
display("Dense view of this small example:",full(L));
display("Unweighted signed physical-edge incidence:",full(B));
assert(norm(full(A)-graphadjacency(G),"fro")==0);
assert(norm(full(L)-graphlaplacian(G),"fro")==0);
assert(norm(sparsemul(L,[1;1;1;1;1]))==0);

% No dense n-by-n array is created by graph generation or graphsparse.
record large=graphgen("path",5000);
record sparse_laplacian=graphsparse(large);
display("Five-thousand-node path; nonzero count:",nnz(sparse_laplacian));
assert(nnz(sparse_laplacian)==14998);

record random_graph=graphgen("gnm",12,options(m=18,seed=2026));
figure(1,"Name","Seeded simple graph","NumberTitle","off");
plotgraph(random_graph,options(layout="random",seed=2026));
title("G(12,18): uniform simple-edge sampling without replacement");

record grid=graphgen("grid",12,options(rows=3,columns=4));
figure(2,"Name","Grid graph and direct sparse operators","NumberTitle","off");
plotgraph(grid,options(layout="grid"));
title("Stable node IDs on a three by four grid");
