INTERACTIVE GRAPHS AND REPRODUCIBLE LAYOUTS

main.p: directed weighted shortest path, stable node/edge identities, labels,
arrowheads and highlighted physical edges. Ctrl-click adds/removes selection;
Escape or the context menu clears graph selection. The Move node toolbar tool
drags a node in the current orthographic camera plane; Rotate remains separate.
Data cursor tips report node ID/label/coordinates or edge ID/endpoints/real weight.

multigraph_positions.p: parallel physical edges, reverse directions and a self
loop, plus a three dimensional sphere layout. Edge IDs are input row numbers;
choosing highlight_edges is unambiguous for a multigraph. The path option alone
chooses the lowest matching physical edge ID at each consecutive node pair.

reproducible_saved_layout.p: seeded random coordinates, schema1 JSON positions,
selection/highlight metadata and strict topology/weight/label matching on restore.
Running it writes interactive_graph_layout.json beside the project data files.
Its explicit overwrite=1 only replaces that generated example artifact.

plotgraph(G[,positions][,options]) retains G unchanged. circle/grid/random/sphere
are deterministic O(n) layouts; random requires a recorded seed for repeatability.
The toolbar's Graph layouts menu applies the same layout functions and saves or
loads dragged positions. A saved layout is data, never executable source. It is
rejected if original node count, edges, real weights, labels or direction changed.
savegraphlayout(G,positions,path[,options]) defaults to refusing existing output;
loadgraphlayout(G,path) returns positions and stable selection/highlight IDs.

Interactive selection/dragging is bounded to1024 nodes and8192 edges. Larger
graphs use an explicit static view, with node_labels=0 and interactive=0; ordinary
plot point/segment budgets still apply. Native PNG, visible SVG and vector PDF
all consume the same graph positions/segments. In3D, vector exports use painter
depth ordering. No force-directed optimization or graph simulation runs on the
GUI thread. Graph numeric weights remain real; Pentarnion states/coupling use
the separately defined network operators, not algebra-valued shortest-path costs.
