#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
int main(){using namespace penta;unsigned checks=0,failures=0;runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-pullback-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));options.odeCacheDirectory=options.dataDirectory/"cache/odes";options.maxOperations=100000000;std::filesystem::create_directories(options.dataDirectory);auto run=[&](const std::string& source,bool expected=true){++checks;const auto result=runtime::execute(source,{},options);if(result.ok()!=expected){++failures;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<" @"<<d.span.offset<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<" @"<<d.span.offset<<": "<<d.message<<'\n';}};
run("native function real field(real y,real scale){return scale*y*y;}native function real map(real x){return x*x*x;}record a=formpullback(field,map,[2.0],1,options(parameters=options(scale=1.0)));record b=formpullback(field,map,[2.0],1,options(parameters=options(scale=2.0)));assert(a.cache_key==b.cache_key);assert(abs(a.second(1)-10752)<1e-8);assert(abs(b.coefficients(1)-1536)<1e-8);assert(a.derivative_order==2);record star=hodge(a);assert(abs(star.coefficients(1)-64)<1e-9);assert(abs(star.first(1)-192)<1e-9);assert(abs(star.second(1)-480)<1e-8);");
run("native function [real a,real b,real c] form(real x,real y,real z){return 1.0,0.0,0.0;}native function [real x,real y,real z] surface(real u,real v){return u,v,u*u+v*v;}record a=formpullback(form,surface,[0.0,0.0],2);assert(a.dimension==2&&a.degree==2);assert(abs(a.coefficients(1)-1)<1e-12);assert(abs(a.metric_second(1,1)-8)<1e-10);record delta=codifferential(a);assert(norm(delta.coefficients,\"fro\")<1e-10);assert(norm(exteriord(a).coefficients,\"fro\")==0);");
run("native function real field(real y){return y;}native function real map(real x){return 0.0;}record a=formpullback(field,map,[2.0],0);",false);
run("native function real field(real x,real y){return x+y;}native function [real x,real y] map(real u,real v){return u,v;}native function [real a,real b,real c,real d] metric(real x,real y){return 1+x*x,0.0,0.0,1+y*y;}record a=formpullback(field,map,[1.0,2.0],0,options(metric=metric));assert(norm(a.metric-[2,0;0,5],\"fro\")<1e-12);assert(abs(a.metric_first(1,1)-2)<1e-12);assert(abs(a.metric_second(4,4)-2)<1e-12);assert(abs(a.second(1,1))<1e-12);");
#ifdef PENTA_SOURCE_DIR
std::ifstream input(std::filesystem::path(PENTA_SOURCE_DIR)/"examples/intrinsic-geometry/native-form-pullback-jets.p");run(std::string((std::istreambuf_iterator<char>(input)),{}));
#endif
std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" form pullback language checks, "<<failures<<" failures\n";return failures?1:0;}
