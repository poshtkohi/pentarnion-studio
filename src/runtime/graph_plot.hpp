#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace penta::runtime {
struct PlotGraph {
    int dimensions=2;
    bool directed=false,multigraph=false,interactive=true,nodeLabels=true,edgeLabels=false;
    std::vector<std::array<double,3>> positions; // Stable node ID = row+1.
    std::vector<std::array<std::uint32_t,2>> edges; // One-based nodes; edge ID = row+1.
    std::vector<double> weights;
    std::vector<std::string> labels;
    std::vector<std::uint32_t> selectedNodes,selectedEdges,highlightedNodes,highlightedEdges;
    std::string layout="circle";
    std::uint64_t seed=0;
    double radius=1,nodeSize=8,edgeWidth=1.5;
};
}
