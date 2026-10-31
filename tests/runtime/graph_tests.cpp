#include "graph/graph_module.hpp"
#ifndef PENTA_GRAPH_NATIVE_ONLY
#include "runtime/interpreter.hpp"
#include "plotting/graph_view.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0;
void expect(bool okay,const std::string& text){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
bool close(double a,double b){return std::abs(a-b)<1e-12*(1+std::abs(b));}
template<class F> void rejects(F action,int status,const char* text){bool found=false;try{action();}catch(const penta::graph::Error& error){found=error.status==status;}expect(found,text);}
struct Fixture {
    std::vector<uint64_t> endpoints;
    std::vector<double> weights;
    penta_graph_request_v1 request{};
    Fixture(std::size_t n,std::vector<uint64_t> edges,std::vector<double> w={},bool directed=false):endpoints(std::move(edges)),weights(std::move(w)){
        request.nodes=n;request.edges=endpoints.size()/2;request.endpoints=endpoints.data();request.weights=weights.empty()?nullptr:weights.data();request.directed=directed;request.max_work=10000000;
    }
    penta::graph::Result run(uint32_t operation,std::size_t capacity=4096){request.operation=operation;return penta::graph::Module().execute(request,capacity);}
};
void native(){
    expect(!penta::graph::Module::isLoaded(),"Graph DLL is not loaded before its first use.");
    Fixture path(3,{1,2,2,3});
    auto info=path.run(PENTA_GRAPH_INFO);expect(penta::graph::Module::isLoaded(),"Graph DLL loads lazily.");
    expect(info.values==std::vector<double>({1,1,1,1,2,2,2,2,1,1,1,1}),"Path degree/in/out/strength oracle.");
    expect(info.report.extra[2]==1&&info.report.scalar==2,"Connected path information.");
    path.request.source=1;path.request.target=3;
    auto shortest=path.run(PENTA_GRAPH_SHORTEST_PATH);expect(shortest.values==std::vector<double>({1,2,3,1,2})&&shortest.report.scalar==2,"Undirected path nodes and physical edge IDs.");
    auto matrix=path.run(PENTA_GRAPH_LAPLACIAN,9);expect(matrix.values==std::vector<double>({1,-1,0,-1,2,-1,0,-1,1}),"Known path Laplacian.");
    const auto adjacency=path.run(PENTA_GRAPH_ADJACENCY,9);expect(adjacency.values==std::vector<double>({0,1,0,1,0,1,0,1,0}),"Known path adjacency.");
    for(uint32_t kind=4;kind<=6;++kind){path.request.kind=kind;path.request.reserved=1;const auto result=path.run(PENTA_GRAPH_CENTRALITY,3);const std::vector<double> expected=kind==4?std::vector<double>{0,1,0}:kind==5?std::vector<double>{2.0/3,1,2.0/3}:std::vector<double>{1.5,2,1.5};for(std::size_t n=0;n<3;++n)expect(close(result.values[n],expected[n]),"Hand-derived path centrality.");}
    path.request.kind=4;path.request.normalized=1;expect(path.run(PENTA_GRAPH_CENTRALITY,3).values==std::vector<double>({0,1,0}),"Normalized three-node betweenness.");
    path.request.kind=0;path.request.dimensions=3;path.request.radius=2;
    const auto layout=path.run(PENTA_GRAPH_LAYOUT,9);for(std::size_t n=0;n<3;++n)expect(close(std::hypot(layout.values[3*n],layout.values[3*n+1]),2)&&layout.values[3*n+2]==0,"Circle layout preserves requested radius in3D.");
    expect(path.run(PENTA_GRAPH_CYCLE,8).report.extra[2]==0,"Undirected parent edge is not a cycle.");
    Fixture triangle(4,{1,2,2,3,1,3},{1,2,4});auto tree=triangle.run(PENTA_GRAPH_SPANNING_TREE,4);
    expect(tree.values==std::vector<double>({1,2})&&tree.report.scalar==3&&tree.report.extra[0]==2&&tree.report.outcome==PENTA_GRAPH_FOREST,"Minimum spanning forest with isolated vertex.");
    auto cycle=triangle.run(PENTA_GRAPH_CYCLE,10);expect(cycle.report.extra[2]==1&&cycle.report.extra[0]==4&&cycle.report.extra[1]==3&&cycle.values.front()==cycle.values[3],"Closed undirected cycle witness.");
    Fixture tie(3,{1,3,1,2,2,3},{1,1,1});expect(tie.run(PENTA_GRAPH_SPANNING_TREE,3).values==std::vector<double>({1,2}),"MST ties use stable input edge IDs.");
    Fixture directed(4,{1,2,2,1,2,3},{1,1,1},true);directed.request.kind=0;auto weak=directed.run(PENTA_GRAPH_COMPONENTS,4);
    expect(weak.values==std::vector<double>({1,1,1,2})&&weak.report.extra[0]==2,"Canonical weak components.");directed.request.kind=1;auto strong=directed.run(PENTA_GRAPH_COMPONENTS,4);
    expect(strong.values==std::vector<double>({1,1,2,3})&&strong.report.extra[0]==3,"Canonical strong components.");
    auto cyclic=directed.run(PENTA_GRAPH_TOPOSORT,4);expect(cyclic.values.empty()&&cyclic.report.outcome==PENTA_GRAPH_CYCLIC,"Cyclic topological sort reports scientific outcome.");
    directed.request.source=3;directed.request.target=1;directed.request.kind=0;auto unreachable=directed.run(PENTA_GRAPH_SHORTEST_PATH,8);expect(unreachable.report.outcome==PENTA_GRAPH_UNREACHABLE&&unreachable.values.empty(),"Unreachable destination has no fabricated path.");
    Fixture dag(4,{1,2,1,3,3,4},{},true);auto ordered=dag.run(PENTA_GRAPH_TOPOSORT,4);bool validOrder=true;for(std::size_t edge=0;edge<dag.endpoints.size();edge+=2)validOrder=validOrder&&std::find(ordered.values.begin(),ordered.values.end(),double(dag.endpoints[edge]))<std::find(ordered.values.begin(),ordered.values.end(),double(dag.endpoints[edge+1]));expect(validOrder&&ordered.values.size()==4,"Topological order satisfies every precedence edge.");
    Fixture negative(3,{1,2,2,3,1,3},{2,-1,5},true);negative.request.source=1;negative.request.target=3;
    auto bellman=negative.run(PENTA_GRAPH_SHORTEST_PATH,6);expect(bellman.report.scalar==1&&std::string(bellman.report.method)=="bellman_ford","Negative acyclic edge uses Bellman-Ford.");negative.request.kind=1;rejects([&]{negative.run(PENTA_GRAPH_SHORTEST_PATH);},PENTA_NUMERIC_ARGUMENT,"Explicit Dijkstra rejects negative weights.");
    Fixture negativeCycle(3,{1,2,2,3,3,2},{1,-2,1},true);negativeCycle.request.source=1;negativeCycle.request.target=3;auto badCycle=negativeCycle.run(PENTA_GRAPH_SHORTEST_PATH,6);expect(badCycle.report.outcome==PENTA_GRAPH_NEGATIVE_CYCLE&&badCycle.values.empty(),"Reachable negative cycle is explicit.");
    Fixture disconnectedCycle(4,{1,2,3,4,4,3},{2,-2,1},true);disconnectedCycle.request.source=1;disconnectedCycle.request.target=2;expect(disconnectedCycle.run(PENTA_GRAPH_SHORTEST_PATH).report.scalar==2,"Unreachable negative cycle does not invalidate a source path.");
    Fixture loop(1,{1,1},{3});auto loopInfo=loop.run(PENTA_GRAPH_INFO,4);expect(loopInfo.values[0]==2&&loopInfo.values[3]==6&&loopInfo.report.extra[0]==1,"Self-loop degree and strength count both incident ends.");expect(loop.run(PENTA_GRAPH_LAPLACIAN,1).values[0]==0&&loop.run(PENTA_GRAPH_ADJACENCY,1).values[0]==3,"Self-loop Laplacian cancellation convention.");expect(loop.run(PENTA_GRAPH_CYCLE,4).values==std::vector<double>({1,1,1}),"Self-loop cycle witness.");
    Fixture multiple(2,{1,2,2,1},{1,2});rejects([&]{multiple.run(PENTA_GRAPH_INFO);},PENTA_NUMERIC_ARGUMENT,"Undirected reverse duplicate requires explicit multigraph.");multiple.request.multigraph=1;expect(multiple.run(PENTA_GRAPH_CYCLE,6).report.extra[2]==1,"Parallel physical edges form an undirected length-two cycle.");
    multiple.request.operation=PENTA_GRAPH_INFO;rejects([&]{penta::graph::Module().execute(multiple.request,1);},PENTA_NUMERIC_ARGUMENT,"Caller output capacity enforced.");
    path.request.kind=0;path.request.operation=PENTA_GRAPH_INFO;path.request.max_work=1;rejects([&]{penta::graph::Module().execute(path.request,12);},PENTA_GRAPH_WORK_LIMIT,"Native work budget terminates graph operation.");path.request.max_work=100000;
    auto cancellation=std::make_shared<std::atomic_bool>(true);rejects([&]{penta::graph::Module().execute(path.request,12,cancellation);},PENTA_NUMERIC_CANCELLED,"Cancellation checked before graph construction.");
    bool originalException=false;try{penta::graph::Module().execute(path.request,12,{},[](std::size_t){throw std::range_error("original budget");});}catch(const std::range_error& error){originalException=std::string(error.what())=="original budget";}expect(originalException,"Host budget exception survives C ABI boundary.");
    Fixture zero(0,{});expect(zero.run(PENTA_GRAPH_COMPONENTS,0).report.extra[0]==0,"Empty graph components.");
    Fixture invalid(2,{1,3});rejects([&]{invalid.run(PENTA_GRAPH_INFO);},PENTA_NUMERIC_ARGUMENT,"Endpoint bounds.");
    Fixture nonfinite(2,{1,2},{std::numeric_limits<double>::infinity()});rejects([&]{nonfinite.run(PENTA_GRAPH_INFO);},PENTA_NUMERIC_ARGUMENT,"Nonfinite weights rejected.");
    rejects([&]{penta::graph::Module(std::filesystem::temp_directory_path()/"penta-graph-missing-module").execute(zero.request,0);},PENTA_NUMERIC_INTERNAL,"Missing optional module has an actionable failure.");
}
#ifndef PENTA_GRAPH_NATIVE_ONLY
penta::runtime::ExecutionResult run(const std::string& source,bool good=true,const penta::runtime::ExecutionOptions& settings={}){
    auto result=penta::runtime::execute(source,{},settings);expect(result.ok()==good,source);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}return result;
}
void language(){
    run("record G=graph([1,2;2,3],options(n=4,labels=options(n1=\"A\",n4=\"isolated\")));record c=components(G);assert(c.count==2);assert(c.labels(4)==2);record p=shortestpath(G,1,3);assert(p.distance==2);assert(p.path(2)==2);record z=shortestpath(G,1,4);assert(z.reachable==0);assert(z.distance_available==0);assert(G.labels.n1==\"A\");");
    run("record G=digraph([1,2;2,3],options(weights=[2,-1]));record p=shortestpath(G,1,3);assert(p.method==\"bellman_ford\");assert(p.distance==1);record t=toposort(G);assert(t.acyclic==1);matrix L=graphlaplacian(G);assert(L(2,2)==-1);assert(L(2,3)==1);");
    run("record G=graph([1,2;2,3]);record b=centrality(G,options(kind=\"betweenness\"));assert(b.scores(2)==1);record f=spanningtree(G);assert(f.spanning==1);record c=graphcycle(G);assert(c.found==0);matrix X=graphlayout(G,options(dimensions=3,radius=2));assert(abs(X(1,1)-2)<1e-12);");
    auto plot=run("record G=digraph([1,2;2,3]);plotgraph(G,[0,0;1,0;1,1]);");if(plot.ok()){const auto& action=plot.figureActions.back();expect(action.graph.has_value(),"Graph plot retains its stable graph descriptor.");if(action.graph){const auto& graph=*action.graph;expect(graph.positions.size()==3&&graph.edges==std::vector<std::array<std::uint32_t,2>>{{1,2},{2,3}}&&graph.directed,"Graph descriptor retains stable nodes and directed endpoints.");expect(graph.positions[1][0]==1&&graph.positions[2][1]==1,"Explicit graph positions retained.");expect(penta::plotting::graphSegments(graph).size()==6,"Directed native geometry retains edge lines and arrowheads.");}}
    run("record G=graph();record s=graphinfo(G);assert(s.nodes==0);record H=graph([],options(n=3));record c=components(H);assert(c.count==3);");
    run("record G=graph([1,2;2,1]);",false);run("record G=graph([1,2],options(weights=[1,2]));",false);run("record G=graph([1,2],options(labels=options(n3=\"bad\")));",false);run("record G=graph([1.5,2]);",false);
    run("record G=graph([1,2]);record t=toposort(G);",false);run("record G=digraph([1,2],options(weights=-1));record p=shortestpath(G,1,2,options(method=\"dijkstra\"));",false);
    run("record G=graph([1,2],options(weights=0));record c=centrality(G,options(kind=\"closeness\"));",false);
    penta::runtime::ExecutionOptions restricted;restricted.maxMatrixElements=100;auto budget=run("record G=graph([],options(n=11));matrix A=graphadjacency(G);",false,restricted);expect(!budget.runtimeDiagnostics.empty()&&budget.runtimeDiagnostics.front().code=="R1005","Dense adjacency is explicitly budgeted.");
    penta::language::AnalysisOptions legacy;legacy.languageVersion="0.1";expect(!penta::language::analyse("record G=graph([1,2]);",legacy).ok(),"Graph additions do not extend profile0.1.");
}
#endif
}
int main(){try{native();
#ifndef PENTA_GRAPH_NATIVE_ONLY
language();
#endif
}catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}std::cout<<checks<<" graph checks, "<<failures<<" failures\n";return failures?1:0;}
