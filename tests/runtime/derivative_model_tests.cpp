#include "runtime/interpreter.hpp"
#include "ode/native_ode.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace {
int checks=0,failures=0;
void expect(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
}
int main(){
    penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-model-partials-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache"/"odes";
    auto run=[&](const std::string& source,bool good=true){const auto result=penta::runtime::execute(source,{},options);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(result.ok()==good,source.substr(0,95));return result;};
    const std::string model="ode F(t,real y;real a){y'=-a*y+t*y*y;}";
    run(model+"matrix J=jacobian(F,2.0,options(parameters=options(a=3.0),time=0.5));assert(size(J,1)==1&&size(J,2)==1);assert(abs(J(1,1)+1)<1e-12);matrix A=jacobian(F,2.0,options(parameters=options(a=3.0),time=0.5,wrt=\"a\"));assert(abs(A(1,1)+2)<1e-12);matrix T=jacobian(F,2.0,options(parameters=options(a=3.0),time=0.5,wrt=\"t\"));assert(abs(T(1,1)-4)<1e-12);");
    run(model+"matrix J=jacobian(F,2.0,options(parameters=options(a=3.0),time=0.5,wrt=[1,2,3]));assert(norm(J-[-1,-2,4])<1e-12);matrix H=hessian(F,2.0,options(parameters=options(a=3.0),time=0.5,wrt=[1,2,3]));assert(norm(H-[1,-1,4;-1,0,0;4,0,0],\"fro\")<1e-12);matrix P=hvp(F,2.0,[1,2,3],options(parameters=options(a=3.0),time=0.5,wrt=[1,2,3]));assert(norm(P-[11;-1;4])<1e-12);");
    run(model+"matrix G=gradient(F,2.0,options(parameters=options(a=3.0),time=0.5));assert(abs(G(1,1)+1)<1e-12);matrix P=jvp(F,2.0,[1,2,3],options(parameters=options(a=3.0),time=0.5,wrt=[1,2,3]));assert(abs(P(1,1)-7)<1e-12);matrix V=vjp(F,2.0,[2],options(parameters=options(a=3.0),time=0.5,wrt=[1,2,3]));assert(norm(V-[-2;-4;8])<1e-12);");
    run(model+"record A=derivative_check(F,2.0,options(parameters=options(a=3.0),time=0.5,wrt=[1,2,3]));record B=derivative_check(F,2.0,options(parameters=options(a=3.0),time=1.0,wrt=[1,2,3],order=2));assert(A.passed&&B.passed);assert(B.cache_hit);assert(A.cache_key==B.cache_key);matrix J=jacobian(F,2.0,options(parameters=options(a=3.0),time=1.0));assert(abs(J(1,1)-1)<1e-12);");
    run("ode coupled(t,real x,real y;real a){x'=a*x+y+t;y'=x-y*y;}matrix J=jacobian(coupled,[1,2],options(parameters=options(a=3.0),time=0.25));assert(norm(J-[3,1;1,-4],\"fro\")<1e-12);matrix A=jacobian(coupled,(1,2),options(parameters=options(a=3.0),time=0.25,wrt=\"a\"));assert(norm(A-[1;0])<1e-12);matrix T=jacobian(coupled,[1,2],options(parameters=options(a=3.0),time=0.25,wrt=\"t\"));assert(norm(T-[1;0])<1e-12);");
    run("ode P(t,pentarnion p;pentarnion A){p'=A*p+p*A;}matrix J=jacobian(P,2.0,options(parameters=options(A=3.0)));assert(norm(J-6*eye(5),\"fro\")<1e-12);matrix A=jacobian(P,2.0,options(parameters=options(A=3.0),wrt=\"A\"));assert(norm(A-4*eye(5),\"fro\")<1e-12);");
    run("ode P(t,pentarnion p;pentarnion A,pentarnion B){p'=A*p+B*(p*p);}record Q=derivative_check(P,1+i-j+ell,options(parameters=options(A=1.0+i,B=0.5+j),time=0.25));assert(Q.passed);record R=derivative_check(P,1+i-j+ell,options(parameters=options(A=1.0+i,B=0.5+j),time=0.25,wrt=\"B\"));assert(R.passed);");
    const auto ordered=run("ode P(t,pentarnion p;pentarnion A,pentarnion B){p'=A*p+B*(p*p);}matrix J=jacobian(P,1+i-j+ell,options(parameters=options(A=1.0+i,B=0.5+j),time=0.25));for(row=1:5){for(col=1:5){display(J(row,col));}}");
    if(ordered.ok()&&!ordered.analysis.program.statements.empty()&&ordered.analysis.program.statements.front().ode){
        penta::ode::NativeOptions native;native.cacheDirectory=options.odeCacheDirectory;
        const auto legacy=penta::ode::evaluateNative(*ordered.analysis.program.statements.front().ode,1,.25,{1,1,-1,0,1},{1,1,0,0,0,.5,0,1,0,0},native);
        expect(ordered.output.size()==25&&legacy.jacobian.size()==25,"two independent compiled Jacobian layouts");
        bool equal=ordered.output.size()==25&&legacy.jacobian.size()==25;
        for(std::size_t n=0;n<ordered.output.size()&&n<legacy.jacobian.size();++n)equal=equal&&std::abs(std::stod(ordered.output[n])-legacy.jacobian[n])<1e-11*(1+std::abs(legacy.jacobian[n]));
        expect(equal,"hyperdual ODE model Jacobian matches established generated ODE Jacobian");
    }else expect(false,"ordered ODE definition retained for independent Jacobian comparison");
    run("ode timed(t,real y){y'=t*y;}matrix J=jacobian(timed,2.0,options(time=0.25));assert(abs(J(1,1)-0.25)<1e-12);matrix T=jacobian(timed,2.0,options(time=0.25,wrt=\"t\"));assert(abs(T(1,1)-2)<1e-12);");
    run(model+"matrix J=jacobian(F,2.0,options(parameters=options(a=3.0)));assert(abs(J(1,1)+3)<1e-12);");
    run(model+"matrix J=jacobian(F,2.0);",false);
    run(model+"matrix J=jacobian(F,2.0,options(parameters=options(a=3.0,b=2.0)));",false);
    run(model+"matrix J=jacobian(F,2.0,options(parameters=options(a=3.0,t=1.0)));",false);
    run(model+"matrix J=jacobian(F,[2,3],options(parameters=options(a=3.0)));",false);
    run(model+"matrix J=jacobian(F,2.0,options(parameters=options(a=3.0),time=\"wrong\"));",false);
    run("ode coupled(t,real x,real y){x'=x;y'=y;}matrix J=jacobian(coupled,1.0);",false);
    run("ode coupled(t,real x,real y){x'=x;y'=y;}matrix J=jacobian(coupled,[1,2,3]);",false);
    run("ode coupled(t,real x,real y){x'=x;y'=y;}matrix J=jacobian(coupled,(1,2,3));",false);
    run("ode P(t,pentarnion p){p'=p*p;}matrix H=hessian(P,i);",false);
    run("ode P(t,pentarnion p){p'=p*p;}matrix H=gradient(P,i);",false);
    run("ode P(t,pentarnion p){p'=p*p;}matrix H=hvp(P,i,[1,0,0,0,0]);",false);
    run(model+"matrix J=jacobian(F,2.0,options(parameters=options(a=3.0),typo=1));",false);
#ifdef PENTA_SOURCE_DIR
    const auto path=std::filesystem::path(PENTA_SOURCE_DIR)/"examples/native-derivatives/ode_partials.p";std::ifstream stream(path,std::ios::binary);const std::string example((std::istreambuf_iterator<char>(stream)),{});expect(!example.empty(),"ODE partials example exists");const auto result=run(example);std::size_t plots=0;for(const auto& action:result.figureActions)if(action.kind==penta::runtime::FigureActionKind::Plot)++plots;expect(plots==2,"ODE partials example retains two plots");
#endif
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" ODE model derivative checks, "<<failures<<" failures\n";return failures?1:0;
}
