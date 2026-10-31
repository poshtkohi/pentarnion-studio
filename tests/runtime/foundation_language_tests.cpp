#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
using namespace penta;
namespace {
int checks=0,failures=0;
void expect(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
void diagnostics(const runtime::ExecutionResult& result){if(result.ok())return;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
void valid(const std::string& source,const std::vector<std::string>& output={}){const auto result=runtime::execute(source);diagnostics(result);expect(result.ok(),source);if(!output.empty())expect(result.output==output,"output: "+source);}
void invalid(const std::string& source){const auto result=language::analyse(source);expect(!result.ok(),"Check rejects: "+source);}
std::map<std::string,std::string> snapshot(const runtime::ExecutionResult& result){std::map<std::string,std::string> data;for(const auto& variable:result.variables)data[variable.name]=variable.type+":"+variable.value;return data;}
}
int main(){
    valid("function real square(real x){return x*x;}callback f=@square;display(f(3));callback sine=@sin;display(sine(0));",{"9","0"});
    valid("real a=2;callback f=@(real x)=>a*x;a=7;clear a;display(f(3));",{"6"});
    valid("matrix A=[2,3];callback f=@(real x)=>A(1)*x;A(1)=99;display(f(4));",{"8"});
    valid("function real apply(callback f,real x){return f(x);}callback square=@(real x)=>x*x;display(apply(square,4));",{"16"});
    valid("function callback multiplier(real a){return @(real x)=>a*x;}callback f=multiplier(3);display(f(7));",{"21"});
    valid("function real subtract(real left,real right){return left-right;}display(subtract(right=2,left=7));callback f=@subtract;display(f(right=3,left=9));",{"5","6"});
    valid("function [real low,real high] bounds(real x){return x-1,x+1;}[real a,real b]=bounds(5);display(a,b);real first=bounds(5);display(first);",{"4 6","4"});
    valid("function real gain(real x,options cfg=options(scale=2.0)){return x*cfg.scale;}display(gain(3));options cfg=options(scale=4.0);display(gain(x=3,cfg=cfg));",{"6","12"});
    valid("function [record data,real metric] summary(real x){return options(value=x),x*x;}[record r,real metric]=summary(3);display(r.value,metric);",{"3 9"});
    invalid("function record f(real x){if(x>0){return options(a=1);}return options(b=1);}");
    valid("options cfg=options(tol=1e-8,steps=100,name=\"demo\");options next=with_options(cfg,tol=1e-10);display(next.steps,get_option(next,\"name\"));",{"100 demo"});
    invalid("function real f(real x){return x;}display(f(y=2));");
    invalid("function real f(real x){return x;}display(f(x=2,x=3));");
    invalid("options x=options(tol=1.0);options y=with_options(x,wrong=2);");
    invalid("function real f(options cfg=options(scale=2.0)){return cfg.scale;}display(f(options(unknown=1))); ");
    invalid("options cfg=options(a=1);cfg=options(b=1);");
    invalid("[real a,real b]=(1,\"bad\");");
    valid("complex z=complex(0,1);assert(z*z==complex(-1));assert(abs(exp(z)-cos(1)-z*sin(1))<1e-12);assert(abs(sqrt(complex(-1))-z)<1e-12);display(realpart(z),imagpart(z));pentarnion p=i;assert(p*p==-1);",{"0 1"});
    valid("assert(abs(log1p(1e-12)-1e-12)<1e-23);assert(expm1(0)==0);assert(erf(0)==0);assert(erfc(0)==1);assert(hypot(3,4)==5);assert(sign(-2)==-1);");
    invalid("complex z=i;");invalid("complex z=complex(0,1);pentarnion p=z;");invalid("display(complex(0,1)==i);");invalid("matrix A=[complex(0,1)];");
    expect(language::analyse("native function complex f(real x){return complex(x);}").ok(),"Native complex results use the descriptor-aware coordinate ABI.");
    invalid("native function real f(real x){while(x>1){x=x-1;}return x;}");
    invalid("native function real f(real x){return f(x);}");
    auto checked=language::analyse("native function real f(real x){real a=x*x;if(a>1){return a;}return x;}");expect(checked.ok(),"Check validates native subset without compilation");
    std::string excessive="native function real too_large(real x){";for(int n=0;n<5001;++n)excessive+="x=x+1.0;";excessive+="return x;}";invalid(excessive);
    auto discovery=runtime::execute("help(\"foundations\");");expect(discovery.ok()&&discovery.output.size()==2&&discovery.output[0].find("snapshot")!=std::string::npos,"foundation help discoverability");
    runtime::InterpreterSession session;expect(session.submit("real a=2;callback f=@(real x)=>a*x;").ok(),"retain callback");auto result=session.submit("clear a;f(4)");diagnostics(result);expect(result.ok()&&result.output==std::vector<std::string>{"8"},"captured callback survives clear across submissions");
    const auto directory=std::filesystem::temp_directory_path()/("penta-foundation-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);
    runtime::ExecutionOptions execution;execution.dataDirectory=directory;
    auto write=[&](const char* filename,const std::string& text){std::ofstream file(directory/filename,std::ios::binary);file<<text;};
    write("helpers.p","function real square(real x){return x*x;}function real fourth(real x){return square(square(x));}real version=1;");
    result=runtime::execute("import \"helpers.p\" as helpers;display(helpers.fourth(3),helpers.version);",{},execution);diagnostics(result);expect(result.ok()&&result.output==std::vector<std::string>{"81 1"},"module namespace and local helper resolution");
    write("danger.p","error(\"Run only\");save(\"should-not-exist.pws\");");language::AnalysisOptions analysis;analysis.sourceDirectory=directory;checked=language::analyse("import \"danger.p\" as danger;",analysis);expect(checked.dependencies.size()==1&&checked.dependencies[0].source&&checked.dependencies[0].source->find("Run only")!=std::string::npos,"exact imported bytes retained for GUI stale guards");expect(checked.ok()&&!std::filesystem::exists(directory/"should-not-exist.pws"),"Check imports source without executing it");
    write("a.p","import \"b.p\" as b;");write("b.p","import \"a.p\" as a;");checked=language::analyse("import \"a.p\" as a;",analysis);expect(!checked.ok(),"cyclic modules rejected");
    analysis.maximumImportBytes=20;checked=language::analyse("import \"helpers.p\" as helpers;",analysis);expect(!checked.ok(),"aggregate module source budget enforced");
    runtime::InterpreterSession saved;result=saved.runFile("complex z=complex(2,3);options cfg=options(scale=2.0,data=[1,2],sin=3,i=4);tuple pair=(1,\"two\");logicalmatrix mask=logical([0,1]);save(\"structured.pws\");",{},execution);diagnostics(result);expect(result.ok(),"save structured pure data");const auto before=snapshot(result);expect(saved.submit("clear;",{},execution).ok(),"clear saved data");result=saved.submit("load(\"structured.pws\");",{},execution);diagnostics(result);expect(result.ok()&&snapshot(result)==before,"structured pure data roundtrip");
    result=saved.submit("options wrong=options(elsewhere=1);save(\"wrong.pws\",\"wrong\");",{},execution);expect(result.ok(),"schema fixture");
    result=saved.submit("callback fn=@sin;save(\"bad.pws\",\"fn\");",{},execution);expect(!result.ok()&&!std::filesystem::exists(directory/"bad.pws"),"callback save rejected before a file is written");
    auto budget=execution;budget.maxMatrixCoordinates=5;result=runtime::execute("options a=options(data=[1,2,3]);options b=options(data=[4,5,6]);",{},budget);expect(!result.ok(),"nested record arrays count against retained storage");
    runtime::InterpreterSession indexed;auto small=execution;small.maxMatrixCoordinates=4;result=indexed.runFile("matrix A=[1,2,3,4];options saved=options(data=A);",{},small);expect(result.ok(),"shared record array alias fits budget");result=indexed.submit("A(1)=9;",{},small);expect(!result.ok(),"indexed copy-on-write counts arrays retained in records");auto unchanged=indexed.submit("display(A(1),saved.data(1));",{},small);diagnostics(unchanged);expect(unchanged.ok()&&unchanged.output==std::vector<std::string>{"1 1"},"rejected indexed write preserves both old aliases");
    language::AnalysisOptions old;old.languageVersion="0.1";checked=language::analyse("integer native=1;integer import=2;integer complex=3;integer threads=4;integer total=native+import+complex+threads;",old);expect(checked.ok(),"0.1 additive names preserve old identifiers");
    std::filesystem::remove_all(directory);std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
