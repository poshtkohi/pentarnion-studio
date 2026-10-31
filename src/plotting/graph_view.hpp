#pragma once
#include "runtime/graph_plot.hpp"
#include <atomic>
#include <functional>
#include <memory>

namespace penta::plotting {
constexpr std::size_t graphInteractiveNodes=1024,graphInteractiveEdges=8192;
void validateGraphView(const runtime::PlotGraph&);
std::vector<std::array<double,3>> graphPositions(std::size_t nodes,int dimensions,
    const std::string& method,double radius=1,std::uint64_t seed=0,
    const std::shared_ptr<std::atomic_bool>& cancellation={});
struct GraphSegment {std::array<double,3> first,last;std::uint32_t edgeId=0;bool highlighted=false,selected=false;};
std::vector<GraphSegment> graphSegments(const runtime::PlotGraph&);
// Curve arc midpoints exclude arrowhead strokes and retain physical edge IDs.
std::vector<std::array<double,3>> graphEdgeAnchors(const runtime::PlotGraph&,
    const std::vector<GraphSegment>&);
std::string graphNodeLabel(const runtime::PlotGraph&,std::uint32_t node);
void highlightGraphPath(runtime::PlotGraph&,const std::vector<std::uint32_t>& orderedNodeIds);
std::string graphLayoutJson(const runtime::PlotGraph&);
runtime::PlotGraph graphLayoutFromJson(const std::string&,const runtime::PlotGraph& original);
}
