#include "plotting/graph_view.hpp"
#include "plotting/native_export.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#ifndef PENTA_GRAPH_VIEW_NATIVE_ONLY
#include "runtime/interpreter.hpp"
#endif
namespace {
int checks=0,failures=0;
void expect(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
template<class F>void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const penta::plotting::ExportError&){rejected=true;}expect(rejected,message);}
penta::runtime::PlotGraph sample(){penta::runtime::PlotGraph g;g.directed=true;g.positions={{0,0,0},{1,0,0},{1,1,0}};g.labels={"start","corner","end"};g.edges={{1,2},{2,3}};g.weights={2,3};g.layout="manual";return g;}
void core(){
    using namespace penta::plotting;auto graph=sample();validateGraphView(graph);
    const auto circle=graphPositions(4,2,"circle",2);expect(std::abs(circle[0][0]-2)<1e-14&&std::abs(circle[1][1]-2)<1e-14,"Circle retains known mathematical coordinates.");
    const auto grid=graphPositions(4,2,"grid");expect(grid==std::vector<std::array<double,3>>{{-1,-1,0},{1,-1,0},{-1,1,0},{1,1,0}},"Grid has deterministic row-major node identities.");
    const auto random=graphPositions(8,3,"random",2,89);expect(random==graphPositions(8,3,"random",2,89)&&random!=graphPositions(8,3,"random",2,90),"Seeded layout is reproducible and changes only positions.");
    for(const auto& p:graphPositions(17,3,"sphere",3))expect(std::abs(std::hypot(p[0],p[1],p[2])-3)<1e-12,"Sphere layout retains requested radius.");
    auto segments=graphSegments(graph);expect(segments.size()==6,"Each directed edge has a line and two native arrowhead segments.");expect(segments[0].first==graph.positions[0]&&segments[0].last==graph.positions[1]&&segments[0].edgeId==1,"Native edge geometry preserves stable endpoints and identity.");
    highlightGraphPath(graph,{1,2,3});expect(graph.highlightedNodes==std::vector<std::uint32_t>{1,2,3}&&graph.highlightedEdges==std::vector<std::uint32_t>{1,2},"Path highlighting references real physical edge IDs.");
    rejects([&]{highlightGraphPath(graph,{3,2});},"Directed path cannot silently reverse an edge.");
    expect(graphNodeLabel(graph,2)=="2: corner","Displayed labels contain stable node IDs.");graph.selectedNodes={2};graph.selectedEdges={1};graph.edgeLabels=true;
    const auto json=graphLayoutJson(graph);const auto restored=graphLayoutFromJson(json,sample());expect(restored.positions==graph.positions&&restored.weights==graph.weights&&restored.labels==graph.labels&&restored.edges==graph.edges&&restored.selectedNodes==graph.selectedNodes&&restored.highlightedEdges==graph.highlightedEdges,"Versioned layout round trip retains identities, weights, positions and selection.");
    auto changed=sample();changed.weights[0]=4;rejects([&]{graphLayoutFromJson(json,changed);},"Changed weights reject a saved layout transactionally.");changed=sample();changed.labels[1]="changed";rejects([&]{graphLayoutFromJson(json,changed);},"Changed labels reject a saved layout.");changed=sample();changed.edges[0]={1,3};rejects([&]{graphLayoutFromJson(json,changed);},"Changed topology rejects a saved layout.");
    auto meta=nlohmann::json::parse(json);meta["selected_nodes"]={1.5};rejects([&]{graphLayoutFromJson(meta.dump(),sample());},"Fractional saved identity is rejected rather than truncated.");meta=nlohmann::json::parse(json);meta["schema_version"]=2;rejects([&]{graphLayoutFromJson(meta.dump(),sample());},"Unknown layout schema is rejected.");meta=nlohmann::json::parse(json);meta["positions"][0][0]=nullptr;rejects([&]{graphLayoutFromJson(meta.dump(),sample());},"Missing numeric coordinate is not coerced to zero.");
    auto multigraph=sample();multigraph.multigraph=true;multigraph.edges={{1,2},{1,2},{2,1},{2,2}};multigraph.weights={1,2,3,4};const auto curved=graphSegments(multigraph);expect(curved.size()==64,"Parallel edges and self loops retain bounded curved geometry and arrows.");for(std::uint32_t id=1;id<=4;++id)expect(std::any_of(curved.begin(),curved.end(),[&](const auto& s){return s.edgeId==id;}),"Each physical multigraph edge survives geometry generation.");
    const auto anchors=graphEdgeAnchors(multigraph,curved);expect(anchors.size()==4&&std::abs(anchors[0][1]-anchors[1][1])>.02&&std::abs(anchors[1][1]-anchors[2][1])>.02,"Physical parallel-edge labels use distinct curve arc midpoints.");expect(anchors[3][0]>multigraph.positions[1][0]+.05,"Self-loop label sits on its arc instead of covering the node.");std::vector<GraphSegment> firstEdge;for(const auto& segment:curved)if(segment.edgeId==1)firstEdge.push_back(segment);const auto& arrow=firstEdge.back();expect(std::hypot(arrow.last[0]-arrow.first[0],arrow.last[1]-arrow.first[1],arrow.last[2]-arrow.first[2])>.04,"Curved arrowheads scale with the complete edge path instead of one tiny subdivision.");expect(multigraph.positions==sample().positions&&multigraph.weights==std::vector<double>{1,2,3,4},"Label/arrow rendering leaves scientific graph positions and weights unchanged.");
    auto large=sample();large.positions=graphPositions(1025,2,"circle");large.labels.resize(1025);large.interactive=false;large.nodeLabels=false;validateGraphView(large);expect(graphSegments(large).size()==6,"Large view retains a static native fallback.");large.interactive=true;rejects([&]{validateGraphView(large);},"Interactive node limit is explicit.");
    auto invalid=sample();invalid.positions[0][0]=std::numeric_limits<double>::infinity();rejects([&]{validateGraphView(invalid);},"Nonfinite positions are rejected.");invalid=sample();invalid.selectedNodes={1,1};rejects([&]{validateGraphView(invalid);},"Duplicate selection IDs are rejected.");
    NativePanel panel;panel.axes={-1,-1,-1};panel.graph=graph;panel.graph->highlightedNodes={1};panel.style.equalUnits=true;panel.title="Directed stable identities";PngOptions image;image.width=500;image.height=400;const auto pixels=rasterRgb({panel},image);std::size_t red=0,orange=0,blue=0;for(std::size_t i=0;i<pixels.size();i+=3){if(pixels[i]>190&&pixels[i+1]<80&&pixels[i+2]<100)++red;if(pixels[i]>200&&pixels[i+1]>100&&pixels[i+1]<180&&pixels[i+2]<80)++orange;if(pixels[i+2]>150&&pixels[i]<90)++blue;}expect(red>10&&orange>10,"Headless PNG paints highlighted paths and independent selection colors.");expect(blue>0,"Native PNG paints unselected graph nodes.");
    penta::runtime::FigureAction action;action.kind=penta::runtime::FigureActionKind::Plot;action.graph=graph;action.dimensions=2;action.axes={-1,-1,-1};NativeFigureScene scene;scene.ingest(action);expect(scene.panels()[0].graph->edges==graph.edges,"Headless scene consumes the same stable graph descriptor.");
}
#ifndef PENTA_GRAPH_VIEW_NATIVE_ONLY
void language(){
    const auto directory=std::filesystem::temp_directory_path()/"penta-graph-view-test";std::filesystem::create_directories(directory);penta::runtime::ExecutionOptions options;options.dataDirectory=directory;
    const std::string source="record G=digraph([1,2;2,3],options(weights=[2,3],labels=options(n1=\"start\",n3=\"end\")));matrix X=graphlayout(G,options(layout=\"grid\"));record path=shortestpath(G,1,3);plotgraph(G,X,options(path=path.path,selected_nodes=[2],edge_labels=1));savegraphlayout(G,X,\"view.json\",options(path=path.path,overwrite=1));record layout=loadgraphlayout(G,\"view.json\");assert(layout.positions(2,1)==X(2,1));plotgraph(G,layout.positions,options(highlight_edges=layout.highlighted_edges));";
    auto result=penta::runtime::execute(source,{},options);expect(result.ok(),"Extended graph language performs layout, path highlighting and saved layout round trip.");if(!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}else expect(result.figureActions.back().graph&&result.figureActions.back().graph->weights==std::vector<double>{2,3},"Language plotting keeps real weights and graph identity metadata.");
    expect(!penta::runtime::execute("record G=digraph([1,2]);plotgraph(G,options(path=[2,1]));").ok(),"Language reports an impossible directed highlighted path.");
    std::filesystem::remove(directory/"view.json");std::filesystem::remove(directory);
}
#endif
}
int main(){try{core();
#ifndef PENTA_GRAPH_VIEW_NATIVE_ONLY
language();
#endif
}catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}std::cout<<checks<<" graph view checks, "<<failures<<" failures\n";return failures?1:0;}
