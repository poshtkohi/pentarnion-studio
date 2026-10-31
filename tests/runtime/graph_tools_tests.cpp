#include "graph/graph_tools.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#ifndef PENTA_GRAPH_TOOLS_NATIVE_ONLY
#include "runtime/interpreter.hpp"
#endif
namespace {
int checks=0,failures=0;
void expect(bool value,const char* text){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
template<class F>void rejects(F f,const char* text){bool rejected=false;try{f();}catch(const penta::graph_tools::Error&){rejected=true;}expect(rejected,text);}
void native(){
    using penta::graph_tools::Module;Module module;std::vector<uint64_t> edges={1,2,2,3};std::vector<double> weights={2,3};penta_graph_tools_request_v1 r{};r.operation=PENTA_GRAPH_TOOLS_SPARSE;r.kind=PENTA_GRAPH_SPARSE_LAPLACIAN;r.nodes=3;r.edges=2;r.endpoints=edges.data();r.weights=weights.data();r.max_work=1000000;
    auto lap=module.execute(r,7,4,0);expect(lap.pointers==std::vector<uint64_t>{0,2,5,7}&&lap.indices==std::vector<uint64_t>{0,1,0,1,2,1,2}&&lap.values==std::vector<double>{2,-2,-2,5,-3,-3,3},"Direct weighted CSC Laplacian matches a hand-derived operator.");
    r.kind=PENTA_GRAPH_SPARSE_ADJACENCY;r.directed=1;auto adjacency=module.execute(r,4,4,0);expect(adjacency.pointers==std::vector<uint64_t>{0,0,1,2}&&adjacency.indices==std::vector<uint64_t>{0,1}&&adjacency.values==weights,"Directed adjacency retains A(source,target) orientation.");
    r.kind=PENTA_GRAPH_SPARSE_INCIDENCE;auto incidence=module.execute(r,4,3,0);expect(incidence.pointers==std::vector<uint64_t>{0,2,4}&&incidence.indices==std::vector<uint64_t>{0,1,1,2}&&incidence.values==std::vector<double>{-1,1,-1,1},"Signed incidence has negative source and positive target per physical edge.");r.weighted=1;expect(module.execute(r,4,3,0).values==std::vector<double>{-2,2,-3,3},"Explicit weighted incidence scales each entire signed column.");
    std::vector<uint64_t> loops={1,1};std::vector<double> one={9};r.nodes=1;r.edges=1;r.endpoints=loops.data();r.weights=one.data();expect(module.execute(r,2,2,0).values.empty(),"Self loop incidence cancels exactly without stored zeros.");r.kind=PENTA_GRAPH_SPARSE_LAPLACIAN;expect(module.execute(r,3,2,0).values.empty(),"Self loop graph Laplacian cancels exactly.");
    r={};r.operation=PENTA_GRAPH_TOOLS_GENERATE;r.kind=PENTA_GRAPH_GENERATE_PATH;r.nodes=5;r.generated_weight=1;auto path=module.execute(r,0,0,8);expect(path.endpoints==std::vector<uint64_t>{1,2,2,3,3,4,4,5},"Path generator retains ordered physical edge IDs.");r.kind=PENTA_GRAPH_GENERATE_CYCLE;expect(module.execute(r,0,0,10).endpoints.back()==1,"Cycle closes the last-to-first edge.");r.kind=PENTA_GRAPH_GENERATE_GRID;r.nodes=6;r.grid_rows=2;r.grid_columns=3;expect(module.execute(r,0,0,24).report.edges==7,"Two by three grid has seven simple physical edges.");
    r={};r.operation=PENTA_GRAPH_TOOLS_GENERATE;r.kind=PENTA_GRAPH_GENERATE_GNM;r.nodes=20;r.requested_edges=35;r.seed=123;r.generated_weight=1;auto generated=module.execute(r,0,0,70);expect(generated.endpoints==module.execute(r,0,0,70).endpoints,"G(n,m) sampling is reproducible for an explicit seed.");r.seed=124;expect(generated.endpoints!=module.execute(r,0,0,70).endpoints,"Changing seed changes the sampled topology.");std::set<std::pair<uint64_t,uint64_t>> unique;for(std::size_t e=0;e<generated.endpoints.size();e+=2){const auto a=generated.endpoints[e],b=generated.endpoints[e+1];expect(a<b&&a>=1&&b<=20,"Undirected simple universe has no self loops or reversed duplicates.");unique.emplace(a,b);}expect(unique.size()==35,"G(n,m) samples exactly m distinct physical edges.");
    r.nodes=4;r.requested_edges=6;r.seed=1;auto complete=module.execute(r,0,0,12);expect(complete.endpoints==std::vector<uint64_t>{1,2,1,3,1,4,2,3,2,4,3,4},"Full undirected edge universe maps to every unique endpoint pair.");r.directed=1;r.nodes=3;r.requested_edges=6;expect(module.execute(r,0,0,12).endpoints==std::vector<uint64_t>{1,2,1,3,2,1,2,3,3,1,3,2},"Full directed edge universe includes both directions without loops.");r.requested_edges=7;rejects([&]{module.execute(r,0,0,14);},"Impossible G(n,m) rejects before any random retry loop.");r.requested_edges=3;r.max_work=1;rejects([&]{module.execute(r,0,0,6);},"Generator obeys native work budget.");r.max_work=100000;auto cancel=std::make_shared<std::atomic_bool>(true);rejects([&]{module.execute(r,0,0,6,cancel);},"Native generator cancellation is checked.");
    r={};r.operation=PENTA_GRAPH_TOOLS_GENERATE;r.kind=PENTA_GRAPH_GENERATE_PATH;r.nodes=50000;r.generated_weight=1;r.max_work=1000000;auto large=module.execute(r,0,0,99998);std::vector<double> largeWeights(49999,1);r.operation=PENTA_GRAPH_TOOLS_SPARSE;r.kind=PENTA_GRAPH_SPARSE_LAPLACIAN;r.edges=49999;r.endpoints=large.endpoints.data();r.weights=largeWeights.data();auto largeL=module.execute(r,149998,50001,0);expect(largeL.values.size()==149998&&largeL.pointers.size()==50001,"Fifty-thousand-node Laplacian uses linear sparse storage without an N squared matrix.");
}
#ifndef PENTA_GRAPH_TOOLS_NATIVE_ONLY
void language(){
    auto result=penta::runtime::execute("record G=graphgen(\"path\",3,options(weight=2));record L=graphsparse(G);matrix dense=full(L);assert(norm(dense-graphlaplacian(G),\"fro\")==0);assert(norm(sparsemul(L,[1;1;1]))==0);record B=graphsparse(G,options(kind=\"incidence\"));assert(norm(full(B)-[-1,0;1,-1;0,1],\"fro\")==0);record A=graphsparse(G,options(kind=\"adjacency\"));assert(norm(full(A)-graphadjacency(G),\"fro\")==0);record D=graphgen(\"gnm\",8,options(directed=1,m=12,seed=7));assert(D.edge_count==12);record H=graphgen(\"grid\",6,options(rows=2,columns=3));assert(H.edge_count==7);");expect(result.ok(),"Graph sparse/generator DSL feeds existing full and sparsemul interfaces.");if(!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
}
#endif
}
int main(){try{native();
#ifndef PENTA_GRAPH_TOOLS_NATIVE_ONLY
language();
#endif
}catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}std::cout<<checks<<" graph sparse/generator checks, "<<failures<<" failures\n";return failures?1:0;}
