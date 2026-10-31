use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

record G = graph([1,2; 2,3; 3,4; 4,1; 1,3],options(weights=[1,1,1,1,2]));
matrix X = graphlayout(G,options(layout="random",seed=2026,radius=2));

% This example intentionally replaces its own generated JSON on repeated runs.
savegraphlayout(G,X,"interactive_graph_layout.json",options(highlight_edges=[1,2],overwrite=1));
record saved = loadgraphlayout(G,"interactive_graph_layout.json");
figure(1,"Name","Reproducible graph layout","NumberTitle","off");
plotgraph(G,saved.positions,options(highlight_edges=saved.highlighted_edges));
title("Saved positions keep stable node and edge identities");
caption("The plot's Graph layouts menu also saves/restores dragged positions and selections.");
display("Restored positions:",saved.positions);

% PNG works through the native headless renderer; SVG/PDF use the visible view.
% exportgraphics("interactive_graph.png",options(overwrite=1));
