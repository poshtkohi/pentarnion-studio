Direct sparse graph operators and seeded generators

graphsparse(G,options(kind="adjacency"/"incidence"/"laplacian")) constructs the
existing canonical CSC record directly; no dense n-by-n temporary is built.
Use sparsemul, sparsesolve, full and nnz through the ordinary sparse interfaces.
Adjacency is A(source,target); Laplacian is out-degree minus adjacency. Signed
incidence has -1 at each physical edge's source, +1 at its target, and zero for
a self loop. Incidence uses each stored undirected edge's input orientation.
Explicit weighted=1 scales incidence columns by their real edge weights; this
is not an implicit square-root factorization of a directed Laplacian. Multiple
edges coalesce in adjacency/Laplacian and remain separate incidence columns.
Exact cancellation zeros are omitted. Negative real weights stay mathematical
operator entries; shortest-path and diffusion routines apply their own rules.

graphgen("path"/"cycle"/"grid"/"gnm",n,options(...)) creates immutable graph-v1
records. Grid requires rows*columns=n and connects right/down neighbours; a
directed grid uses those forward directions. G(n,m) samples the explicit simple
loop-free edge universe uniformly without replacement, using a recorded seed.
Stable physical edge IDs follow sorted sampled universe IDs. Random topology
and random display coordinates are independent explicit seeds. Limits are
100000 nodes/500000 physical edges; canonical CSC has at most99999 columns.
Graphs with a larger incidence column count must be processed in smaller blocks.
