#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
int main(){using namespace penta;unsigned checks=0,failures=0;runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-manifold-forms-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));options.odeCacheDirectory=options.dataDirectory/"cache/odes";options.maxOperations=100000000;std::filesystem::create_directories(options.dataDirectory);auto run=[&](const std::string& source,runtime::InterpreterSession* session=nullptr){++checks;const auto result=session?session->submit(source,{},options):runtime::execute(source,{},options);if(!result.ok()){++failures;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<" @"<<d.span.offset<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<" @"<<d.span.offset<<": "<<d.message<<'\n';}};
run("native function matrix<2,2> metric(real r,real theta){return [1,0;0,r*r];}native function matrix<1,2> radial(real r,real theta){return [1,0];}native function real potential(real r,real theta){return r*r;}record a=formfield(radial,[2,.4],1,options(metric=metric));record b=hodge(a);assert(b.format==\"PentarnionManifoldForm-v2\");assert(norm(b.coefficients-[0,2],\"fro\")<1e-12);assert(abs(b.first(2,1)-1)<1e-12);record c=hodge(b);assert(norm(c.coefficients+a.coefficients,\"fro\")<1e-12);assert(norm(c.first,\"fro\")<1e-12);record d=codifferential(a);assert(abs(d.coefficients(1)+.5)<1e-12);assert(abs(d.first(1,1)-.25)<1e-12);record f=formfield(potential,[2,.4],0,options(metric=metric));record l=formlaplacian(f);assert(abs(l.coefficients(1)+4)<1e-12);assert(l.derivative_order==0);record dd=exteriord(exteriord(a));assert(size(dd.coefficients,2)==0);save(\"form.pws\",\"a\");");
run("record a=dform(2,1,[1,0]);record b=hodge(a);assert(b.format==\"PentarnionRealForm-v1\");assert(b.coefficients==[0,1]);record d=codifferential(a);assert(d.coefficients(1)==0);");
runtime::InterpreterSession restoredSession;run("load(\"form.pws\");",&restoredSession);run("record restored=codifferential(a);assert(abs(restored.coefficients(1)+.5)<1e-12);",&restoredSession);
#ifdef PENTA_SOURCE_DIR
std::ifstream input(std::filesystem::path(PENTA_SOURCE_DIR)/"examples/intrinsic-geometry/varying-metric-differential-forms.p");run(std::string((std::istreambuf_iterator<char>(input)),{}));
#endif
std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" manifold forms language checks, "<<failures<<" failures\n";return failures?1:0;}
