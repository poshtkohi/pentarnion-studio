#include "runtime/interpreter.hpp"
#include "native_functions/native_callback.hpp"
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <iostream>
namespace {
using namespace penta;
int checks=0,failures=0;
void expect(bool okay,const std::string& text){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
void show(const runtime::ExecutionResult& result){for(const auto& diagnostic:result.analysis.diagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<'\n';for(const auto& diagnostic:result.runtimeDiagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<'\n';}
void languageCases(const std::filesystem::path& directory){
    runtime::ExecutionOptions options;options.dataDirectory=directory;options.odeCacheDirectory=directory/"odes";
    auto run=[&](const std::string& text,bool valid=true){const auto result=runtime::execute(text,{},options);if(valid&&!result.ok())show(result);expect(result.ok()==valid,text);return result;};
    run("record f=cas(\"x^3+sin(x)+exp(x)\",options(symbols=\"x\"));record d=casdiff(f,\"x\");real value=casreal(d,options(x=0.3));assert(abs(value-(3*0.3^2+cos(0.3)+exp(0.3)))<1e-12);");
    run("record f=cas(\"1/x\",options(symbols=\"x\",nonzero=\"x\"));record d=casdiff(f,\"x\");assert(abs(casreal(d,options(x=2))+0.25)<1e-12);assert(f.nonzero==\"x\");");
    run("record f=cas(\"sqrt(x)+log(x)\",options(symbols=\"x\",positive=\"x\"));record d=casdiff(f,\"x\");assert(abs(casreal(d,options(x=4))-0.5)<1e-12);");
    run("record f=cas(\"f(x)\",options(symbols=\"x\",functions=\"f\"));record d=casdiff(f,\"x\");display(d.expression);");
    run("record z=cas(\"sqrt(-1)\",options(domain=\"complex\"));complex value=cascomplex(z);assert(abs(realpart(value))<1e-12);assert(abs(imagpart(value)-1)<1e-12);");
    run("record p=caspent(\"i*j\",options(gamma=\"symbolic\"));pentarnion value=caspentnumeric(p,options(gamma=1));assert(norm2(value-i*j)<1e-24);record d=casdiff(p,\"gamma\");record v=casnumeric(d,options(gamma=0.4));assert(abs(v.real(4)+0.4/(1.16^1.5))<1e-12);assert(abs(v.real(5)-1/(1.16^1.5))<1e-12);");
    run("record p=caspent(\"(p*q)*r-p*(q*r)\",options(pentarnions=\"p,q,r\",gamma=\"symbolic\"));record v=casnumeric(p,options(gamma=1,p0=0,p1=1,p2=0,p3=0,p4=0,q0=0,q1=0,q2=1,q3=0,q4=0,r0=0,r1=0,r2=0,r3=0,r4=1));pentarnion reference=associator(i,j,ell);assert(abs(v.real(1)-coeff(reference,0))<1e-12);assert(abs(v.real(4)-coeff(reference,3))<1e-12);");
    run("record A=casmatrix(\"a,1;1,1\",options(symbols=\"a\"));record B=casmatrix(\"1;0\",options(symbols=\"a\"));record x=caslinsolve(A,B);record value=casnumeric(x,options(a=3));assert(norm(value.real-[0.5;-0.5])<1e-12);record check=casnumeric(casmatmul(A,x),options(a=3));assert(norm(check.real-[1;0])<1e-12);assert(casreal(casdet(A),options(a=3))==2);");
    run("record f=cas(\"1/x\",options(symbols=\"x\"));",false);
    run("record f=cas(\"1/x\",options(symbols=\"x\",nonzero=\"x\"));record changed=cassubs(f,\"x\",\"0\");",false);
    run("record f=cas(\"log(x)\",options(symbols=\"x\",positive=\"x\"));real x=casreal(f,options(x=-1));",false);
    run("record f=cas(\"sqrt(z)\",options(symbols=\"z\",nonzero=\"z\",domain=\"complex\"));record d=casdiff(f,\"z\");complex x=cascomplex(d,options(z=complex(-1,0)));",false);
    run("record f=caspent(\"i*j\",options(gamma=\"symbolic\"));string code=cascode(f);",false);
    runtime::InterpreterSession stored;auto first=stored.submit("record f=cas(\"1/x\",options(symbols=\"x\",nonzero=\"x\"));save(\"symbolic-domain.pws\",\"f\");",{},options);if(!first.ok())show(first);expect(first.ok(),"Symbolic expression and explicit domain persist as data.");runtime::InterpreterSession restored;auto loaded=restored.submit("load(\"symbolic-domain.pws\");",{},options);expect(loaded.ok(),"Saved symbolic domain loads.");const auto checked=restored.submit("assert(casreal(f,options(x=2))==0.5);",{},options);if(!checked.ok())show(checked);expect(checked.ok(),"Restored symbolic payload retains numeric assumption checking.");
    auto codeCase=[&](const std::string& text,const std::vector<double>& point,const std::vector<double>& expected){const auto generated=run(text);if(!generated.ok()||generated.output.empty())return;const auto parsed=language::analyse(generated.output.front());if(!parsed.ok()){for(const auto& d:parsed.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(parsed.ok(),"Generated native source passes typed analysis.");for(const auto& statement:parsed.program.statements)if(statement.function){native_functions::Options build;build.build.cacheDirectory=directory/"exported";native_functions::Callback callback(*statement.function,build);native_functions::DerivativeRequest value;value.order=0;const auto result=callback.evaluate(parsed.program.gamma,point,value);bool same=result.values.size()==expected.size();for(std::size_t n=0;same&&n<expected.size();++n)same=std::abs(result.values[n]-expected[n])<1e-10;expect(same,"Symbolic code executes through a real cached native shared module.");}};
    codeCase("record f=cas(\"x^3+sin(x)\",options(symbols=\"x\"));display(cascode(f,\"exported_scalar\"));",{.3},{.3*.3*.3+std::sin(.3)});
    codeCase("record f=casmatrix(\"a,1;1,1\",options(symbols=\"a\"));record b=casmatrix(\"1;0\",options(symbols=\"a\"));display(cascode(caslinsolve(f,b),\"exported_system\"));",{3},{.5,-.5});
    codeCase("record f=caspent(\"i*j\",options(gamma=\"symbolic\"));record fixed=cassubs(f,\"gamma\",\"1\");display(cascode(fixed,\"exported_pentarnion\"));",{0},{0,0,0,1/std::sqrt(2.),1/std::sqrt(2.)});
    auto small=options;small.maxNativeOperations=1;const auto quota=runtime::execute("record f=cas(\"x*x\",options(symbols=\"x\"));",{},small);expect(!quota.ok(),"Native CAS operation quota is enforced.");
}
}
int main(){const auto directory=std::filesystem::temp_directory_path()/("penta-cas-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));try{std::filesystem::create_directories(directory);languageCases(directory);}catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}std::error_code ignored;std::filesystem::remove_all(directory,ignored);std::cout<<checks<<" native CAS language checks, "<<failures<<" failures\n";return failures?1:0;}
