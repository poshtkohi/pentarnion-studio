#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#ifndef PENTA_SOURCE_DIR
#define PENTA_SOURCE_DIR "."
#endif
namespace {
int checks=0,failures=0;
void check(bool value,const std::string& label){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
void diagnostics(const penta::runtime::ExecutionResult& result){if(result.ok())return;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
}
int main(){
    const auto folder=std::filesystem::temp_directory_path()/("penta-derivative-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);penta::runtime::ExecutionOptions execution;execution.dataDirectory=folder;execution.odeCacheDirectory=folder/".penta-cache"/"odes";
    auto valid=[&](const std::string& source,const std::string& label){const auto result=penta::runtime::execute(source,{},execution);diagnostics(result);check(result.ok(),label);return result;};
    auto invalid=[&](const std::string& source,const std::string& label){const auto result=penta::runtime::execute(source,{},execution);check(!result.ok(),label);return result;};
    try{
        valid("function real f(real x,real y){return x*x+3.0*x*y+y*y;}matrix g=gradient(f,[2.0,3.0]);matrix H=hessian(f,[2.0,3.0]);assert(g==[13.0;12.0]);assert(H==[2.0,3.0;3.0,2.0]);matrix p=hvp(f,[2.0,3.0],[4.0,-2.0]);assert(p==[2.0;8.0]);", "native gradient/Hessian/HVP adapters");
        valid("function[real a,real b] F(real x,real y){return x*x+y,2.0*x-y*y;}matrix J=jacobian(F,[2.0,3.0]);matrix a=jvp(F,[2.0,3.0],[2.0,-1.0]);matrix b=vjp(F,[2.0,3.0],[2.0,-1.0]);assert(J==[4.0,1.0;2.0,-6.0]);assert(a==[7.0;10.0]);assert(b==[6.0;8.0]);", "asymmetric JVP/VJP orientation");
        valid("native function[real a,pentarnion b] F(real x,pentarnion p){return x*x,p*x;}[real a,pentarnion b]=F(2.0,1.0+i);assert(a==4.0);assert(b==2.0+2.0*i);matrix J=jacobian(F,(2.0,1.0+i));assert(size(J,1)==6);assert(size(J,2)==6);assert(J(1,1)==4.0);assert(J(2,1)==1.0);assert(J(3,1)==1.0);", "native tuple invocation and mixed coefficient packing");
        valid("function real f(real x,real gain){return gain*x*x;}matrix gx=gradient(f,2.0,options(parameters=options(gain=3.0)));matrix ga=jacobian(f,2.0,options(parameters=options(gain=3.0),wrt=\"gain\"));assert(gx(1)==12.0);assert(ga(1)==4.0);matrix selected=jacobian(f,[2.0,3.0],options(wrt=[2,1]));assert(selected==[4.0,12.0]);", "fixed runtime parameters and ordered selected partials");
        valid("function real f(real x,real gain=3.0){return gain*x*x;}matrix g=gradient(f,2.0);assert(g(1)==12.0);", "default callback parameters stay runtime values");
        valid("callback f=@(real x)=>sin(x);matrix g=gradient(f,0.0);assert(g(1)==1.0);callback sine=@sin;matrix H=hessian(sine,0.5);assert(abs(H(1)+sin(0.5))<1e-12);", "capture-free lambda and mathematical builtin compile natively");
        valid("function real f(real x){return exp(x);}record c=derivative_check(f,0.3,options(order=2,threads=2));assert(c.passed);assert(c.threads_requested==2);assert(c.threads_used==1);matrix H=hessian(f,0.3,options(method=\"finite_difference\"));assert(abs(H(1)-exp(0.3))<1e-5);", "explicit finite differences and independent derivative verification");
        valid("function real f(real x){return x*x;}threads(2);matrix g=gradient(f,2.0,options(threads=1));assert(threads()==2);", "per-call derivative threads preserve global session settings");
        invalid("function real f(real x){return abs(x);}matrix g=gradient(f,0.0);", "nonsmooth derivative has no silent fallback");
        invalid("real captured=2.0;callback f=@(real x)=>captured*x;matrix g=gradient(f,2.0);", "implicit callback captures require explicit native parameters");
        invalid("function matrix f(real x){return [x,x];}matrix J=jacobian(f,2.0);", "unsupported array-return callbacks diagnosed");
        invalid("function pentarnion f(pentarnion p){return p*p;}matrix H=hessian(f,1.0+i);", "Hessian requires a real scalar objective");
        invalid("function real f(real x){return x*x;}matrix J=jacobian(f,2.0,options(wrt=[1,1]));", "duplicate differentiation coordinates rejected");
        invalid("function real f(real x){return x*x;}matrix J=jacobian(f,2.0,options(wrt=\"missing\"));", "unknown selected parameter diagnosed");
        invalid("function real f(real x){return x*x;}matrix J=jvp(f,2.0,[1.0,2.0]);", "JVP direction shape checked");
        invalid("function real f(real x){return x*x;}matrix J=jacobian(f,2.0,options(threads=65));", "per-call thread limits checked");
        auto limited=execution;limited.maxOperations=100;const auto bounded=penta::runtime::execute("function real f(real x,real y,real z){return exp(x*y+z);}matrix H=hessian(f,[1.0,2.0,3.0]);",{},limited);check(!bounded.ok(),"native derivative work charges host operation budget");
        for(const auto* name:{"main.p","pentarnion_partials.p"}){
            std::ifstream stream(std::filesystem::path(PENTA_SOURCE_DIR)/"examples"/"native-derivatives"/name,std::ios::binary);const std::string source{std::istreambuf_iterator<char>(stream),{}};check(!source.empty(),std::string("example source exists: ")+name);valid(source,std::string("original runnable native-derivatives example: ")+name);
        }
    }catch(const std::exception&e){++failures;std::cerr<<e.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(folder,ignored);
    std::cout<<checks<<" derivative language checks, "<<failures<<" failures\n";return failures?1:0;
}
