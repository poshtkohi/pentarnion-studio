#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
int main(){using namespace penta;unsigned checks=0,failures=0;runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-mesh-query-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));options.odeCacheDirectory=options.dataDirectory/"cache/odes";std::filesystem::create_directories(options.dataDirectory);auto run=[&](const std::string& source){++checks;const auto result=runtime::execute(source,{},options);if(!result.ok()){++failures;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<" @"<<d.span.offset<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}};
run("record mesh=meshplane([0,1],[0,1],options(nx=5,ny=5));matrix v=mesh.vertices;matrix u=v(:,1)+2*v(:,2);record q=meshprobe(mesh,[.2,.3,1],u);assert(abs(q.values(1)-.8)<1e-12);assert(abs(q.distances(1)-1)<1e-12);record s=meshslice(mesh,[1,0,0],.5,options(values=u));assert(s.count==5);assert(s.segment_count==4);assert(norm(s.values-(.5+2*s.points(:,2)),\"fro\")<1e-12);");
run("algebra H=builtin_algebra(\"quaternion\");record mesh=meshplane([0,1],[0,1],options(nx=3,ny=3));matrix v=mesh.vertices;element<H> u0=1+H.i+2*H.j+3*H.k;matrix u=ones(9,1);matrix<H,9,1> field=u*u0;record q=meshprobe(mesh,[.2,.3,1],field);assert(q.field_components==4);assert(norm(q.values-[1,1,2,3],\"fro\")<1e-12);");
run("record sphere=meshsphere(1,options(refinements=1));record q=meshcontains(sphere,[0,0,0;2,0,0]);assert(q.classification==[1;0]);record r=meshray(sphere,[2,0,0],[-1,0,0]);assert(r.classification(1)==1);assert(abs(r.parameters(1)-1)<1e-12);");
#ifdef PENTA_SOURCE_DIR
for(const auto* name:{"probe-and-slice-fields.p","closed-solid-and-rays.p"}){std::ifstream input(std::filesystem::path(PENTA_SOURCE_DIR)/"examples/geometry-queries"/name);run(std::string((std::istreambuf_iterator<char>(input)),{}));}
#endif
std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" mesh query language checks, "<<failures<<" failures\n";return failures?1:0;}
