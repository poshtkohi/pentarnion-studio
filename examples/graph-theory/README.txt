GRAPH PATHS, NETWORKS AND LAPLACIANS

Open project.pprj and run main.p to inspect real weighted undirected/directed
graphs, shortest paths, spanning forests, connected components, centrality,
Laplacians, adjacency matrices, topological ordering and cycle witnesses.
Node IDs are one-based. An edge row is [source,target]. Original edge IDs are
the input row numbers. options(n=...) retains isolated nodes.

Run coupled-pentarnion-network.p for a three-node path diffusion experiment.
Its explicitly declared native ODE preserves all five coefficients, uses a
compiler-generated analytic Jacobian and conserves the network mean. The graph
Laplacian is shown alongside the equation. This example does not imply a
generic graph-to-ODE compiler; its three node equations are written explicitly.

Real weights are path costs. The automatic shortest-path method chooses
Dijkstra for nonnegative weights and Bellman-Ford if any edge is negative.
Reachable negative cycles have status="negative_cycle" and no distance.
Unreachable paths have status="unreachable". Check distance_available before
using the distance field; a missing distance is represented by0 with that flag
off, never by infinity. Bellman-Ford conservatively reports any negative cycle
reachable from the source, including one not leading to the chosen target.

Simple graphs reject duplicate edges; undirected reversed endpoints are the
same edge. Explicit options(multigraph=1) retains parallel physical edges.
spanningtree produces a minimum forest when the graph is disconnected.
Topological ordering requires a digraph. Weighted closeness, harmonic and
betweenness require positive weights; weighted=0 uses hop distances. Node
strength includes both incoming and outgoing weights; an undirected loop
counts twice in degree/strength. Adjacency stores a loop once and the
out-degree-minus-adjacency Laplacian cancels loops.

Graph storage and native algorithms use edge lists/adjacency lists. Only
graphadjacency and graphlaplacian request dense n-by-n matrices and enforce the
normal matrix allowance before allocation. The lazy penta_graph DLL/SO uses
Boost Graph headers. Work and cancellation are bounded. Current algorithms
use one shared CPU budget slot and do not start private worker pools.

plotgraph(G[,positions]) draws node crosses and edges, with arrowheads for a
digraph. Optional positions have n rows and2 or3 columns. graphlayout supplies
a deterministic circle layout. Current figure meshes show numeric node IDs
in the data cursor; custom text labels are retained in G.labels. Interactive
node dragging and alternate automatic layouts are future extensions.
