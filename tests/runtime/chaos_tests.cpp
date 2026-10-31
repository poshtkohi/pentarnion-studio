#include "chaos/chaos_module.hpp"
#ifndef PENTA_CHAOS_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0;
void expect(bool okay,const std::string& message){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
bool close(double a,double b,double tolerance=1e-11){return std::abs(a-b)<=tolerance*(1+std::abs(b));}
void kernels(){
    using namespace penta::chaos;Module module;
    const auto q=module.orthonormalize({std::exp(.25),0,0,std::exp(-.5)},2);
    expect(close(q.logarithms[0],.25)&&close(q.logarithms[1],-.5),"QR log-stretches match independent diagonal exponentials");
    expect(q.orthogonalityError<1e-14,"Householder tangent basis orthogonality");
    const auto mixed=module.orthonormalize({2,3,0,4},2);
    expect(close(mixed.logarithms[0]+mixed.logarithms[1],std::log(8.)),"QR log-volume equals independent determinant");
    auto rejects=[&](auto action,int expected,const char* text){bool rejected=false;try{action();}catch(const Error& error){rejected=error.status==expected;}expect(rejected,text);};
    rejects([&]{(void)module.orthonormalize({1,0,0,0},2);},PENTA_CHAOS_DOMAIN,"collapsed tangent direction diagnoses numerical failure");
    rejects([&]{(void)module.orthonormalize({1,0,0,std::numeric_limits<double>::quiet_NaN()},2);},PENTA_CHAOS_DOMAIN,"nonfinite tangent matrix rejected");
    rejects([&]{(void)module.orthonormalize({1},2);},PENTA_CHAOS_ARGUMENT,"incorrect tangent buffer dimensions rejected");
    const auto recurrence=module.recurrence({0,1,0},3,1,.1,0);
    expect(recurrence.eligible==6&&recurrence.recurrent==2,"recurrence rate excludes diagonal pairs");
    expect(recurrence.matrix==std::vector<double>({0,0,1,0,0,0,1,0,0}),"recurrence matrix independently known sample identities");
    const auto excluded=module.recurrence({0,1,0},3,1,.1,1);
    expect(excluded.eligible==2&&excluded.recurrent==2,"Theiler band changes eligible denominator correctly");
    const auto noPairs=module.recurrence({0,1,0},3,1,.1,2);
    expect(noPairs.eligible==0&&noPairs.recurrent==0,"empty eligible pair set remains explicit");
    rejects([&]{(void)module.recurrence({0,1},2,1,0,0);},PENTA_CHAOS_ARGUMENT,"zero recurrence radius rejected");
    rejects([&]{Module relative("relative");},PENTA_CHAOS_ARGUMENT,"relative optional-module path rejected");
    const auto missing=std::filesystem::temp_directory_path()/("penta-no-chaos-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(missing);
    rejects([&]{(void)Module(missing).orthonormalize({1},1);},PENTA_CHAOS_INTERNAL,"missing chaos module diagnosed");std::filesystem::remove(missing);
}
#ifndef PENTA_CHAOS_KERNEL_ONLY
void runtime(){
    using namespace penta::runtime;ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-chaos-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache"/"odes";options.maxOperations=20000000;
    auto run=[&](const std::string& source,bool good=true){const auto result=execute(source,{},options);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<" at "<<d.span.offset<<": "<<source.substr(d.span.offset,std::min<std::size_t>(d.span.length,160))<<'\n';}expect(result.ok()==good,"dynamical analysis program "+source.substr(0,85));return result;};
    run("ode diagonal(t,real x,real y){x'=-x;y'=-2*y;}record l=lyapunov(diagonal,[0,0],options(t1=2,qr_step=0.25,rtol=1e-10,atol=1e-12));assert(abs(l.exponents(1)+1)<2e-7);assert(abs(l.exponents(2)+2)<2e-7);assert(l.orthogonality_error<1e-12);assert(size(l.history,2)==2);assert(l.status==\"finite_time_estimate\");record q=lyapunov(diagonal,[0,0],options(t1=2,qr_step=0.125,transient=0.5,method=\"adams\",rtol=1e-10,atol=1e-12));assert(norm(q.exponents-l.exponents)<1e-6);assert(q.cache_hit);assert(q.cache_key==l.cache_key);record once=lyapunov(diagonal,[0,0],options(t1=0.25,qr_step=0.25,max_intervals=1));assert(once.intervals==1);record stable=stability(diagonal,[0,0]);assert(stable.stable&&stable.equilibrium);record away=stability(diagonal,[1,0]);assert(away.status==\"not_equilibrium\");");
    run("ode pentflow(t,pentarnion p;real rate){p'=-rate*p;}record l=lyapunov(pentflow,i+j,options(parameters=options(rate=0.5),t1=2,qr_step=0.25));assert(l.dimension==5);assert(norm(l.exponents+0.5*ones(5,1))<1e-5);record s=stability(pentflow,0.0,options(parameters=options(rate=0.5)));assert(s.stable);assert(size(s.jacobian,1)==5);");
    run("ode triangular(t,real x,real y){x'=x+2*y;y'=-2*y;}record l=lyapunov(triangular,[0,0],options(t1=3,qr_step=0.3,rtol=1e-10,atol=1e-12));assert(norm(l.exponents-[1;-2])<2e-6);ode rotation(t,real x,real y){x'=y;y'=-x;}record r=lyapunov(rotation,[1,0],options(t1=3,qr_step=0.4,method=\"adams\",rtol=1e-10,atol=1e-12));assert(norm(r.exponents)<1e-7);");
    run("ode unstable(t,real x){x'=x;}record s=stability(unstable,0.0);assert(s.unstable);ode neutral(t,real x){x'=0;}record n=stability(neutral,0.0);assert(n.inconclusive);native function real contraction(real x){return 0.5*x;}record m=stability(contraction,0.0);assert(m.stable);native function real expansion(real x){return 2*x;}record u=stability(expansion,0.0);assert(u.unstable);");
    run("native function [real a,real b] diagonal(real x,real y){return 2*x,0.5*y;}record l=lyapunov(diagonal,[0,0],options(iterations=50,transient=7,qr_stride=4));assert(norm(l.exponents-[log(2);-log(2)])<1e-12);assert(l.method==\"map\");assert(l.duration==43);record q=lyapunov(diagonal,[0,0],options(iterations=25,qr_stride=1));assert(q.cache_key==l.cache_key&&q.cache_hit);assert(norm(q.exponents-l.exponents)<1e-12);native function pentarnion shrink(pentarnion p){return 0.5*p;}record p=lyapunov(shrink,i,options(iterations=24,transient=3,qr_stride=2));assert(p.dimension==5);assert(norm(p.exponents-log(0.5)*ones(5,1))<1e-12);");
    run("ode oscillator(t,real x,real y){x'=y;y'=-x;}native function real section(real t,real x,real y){return x;}solution s=odesolve(oscillator,x=1.0,y=0.0,t0=0,t1=12,sample_step=0.4,events=section,event_direction=1,method=\"adams\",rtol=1e-10,atol=1e-12);record p=poincare(s);assert(p.crossings==2);assert(abs(p.t(1)-3*acos(-1)/2)<1e-7);assert(abs(p.intervals(1)-2*acos(-1))<1e-7);assert(norm(p.states(:,1))<1e-7);assert(p.direction==[1;1]);record after=poincare(s,options(transient=6.0));assert(after.crossings==1);");
    run("record r=recurrence([0;1;0],options(radius=0.1));assert(r.eligible_pairs==6);assert(abs(r.rate-1/3)<1e-12);assert(r.matrix(1,3)==1);record q=recurrence([0;1;0],options(radius=0.1,lag=2,theiler=1));assert(q.rate==1&&q.distance(1)==0&&q.returned(1)==1);record p=recurrence([i;j;i],options(radius=0.1,lag=2));assert(p.distance(1)==0);");
    run("native function real affine(real x,real a){return 0.5*x+a;}matrix points=linspace(1,32,32)';record serial=ensemble(affine,points,options(parameters=options(a=2.0),iterations=12,threads=1));record parallel=ensemble(affine,points,options(parameters=options(a=2.0),iterations=12,threads=4));assert(serial.states==parallel.states);assert(parallel.runs==32&&parallel.failures==0&&parallel.escapes==0);assert(parallel.cache_hit);assert(parallel.cache_key==serial.cache_key);assert(parallel.threads_used>=1&&parallel.threads_used<=4);assert(abs(parallel.states(1)-(4+(1-4)/4096))<1e-12);record changed=ensemble(affine,[0;1],options(parameters=options(a=3.0),iterations=1));assert(changed.cache_key==serial.cache_key&&changed.cache_hit);assert(changed.states==[3;3.5]);");
    run("native function real reciprocal(real x){return 1/x;}record e=ensemble(reciprocal,[1;0;2],options(threads=3));assert(e.success==[1;0;1]);assert(e.failures==1);assert(e.states(1)==1&&e.states(3)==0.5);native function real grow(real x){return 2*x;}record g=ensemble(grow,[0.1;0.2],options(iterations=10,escape_radius=1.0));assert(g.escapes==2&&g.failures==0);assert(g.escaped==[1;1]);");
    run("native function pentarnion contraction(pentarnion p){return 0.5*p;}record e=ensemble(contraction,[1,2,3,4,5;2,4,6,8,10],options(iterations=2));assert(e.states==[0.25,0.5,0.75,1,1.25;0.5,1,1.5,2,2.5]);");
    run("ode f(t,real x){x'=-x;}record a=lyapunov(f,0.0,options(qr_step=0));",false);
    run("ode f(t,real x){x'=-x;}record a=lyapunov(f,0.0,options(method=\"erk\"));",false);
    run("ode f(t,real x){x'=-x;}record a=lyapunov(f,0.0,options(transient=10,t1=10));",false);
    run("record r=recurrence([1;2],options(radius=0));",false);
    run("native function real f(real x){return x;}record l=lyapunov(f,0.0,options(iterations=4,transient=4));",false);
    run("native function real f(real x){return 0*x;}record l=lyapunov(f,1.0,options(iterations=4));",false);
    run("native function real f(real x){return x;}record e=ensemble(f,[1;2],options(typo=1));",false);
    run("native function real f(real x){return x;}record e=ensemble(f,[1;2],options(threads=65));",false);
    const auto savedBudget=options.maxOperations;options.maxOperations=250;
    run("native function real f(real x){return 0.5*x;}record e=ensemble(f,ones(16,1),options(iterations=100,threads=4));",false);options.maxOperations=savedBudget;
#ifdef PENTA_SOURCE_DIR
    for(const auto& relative:{"examples/chaos-analysis/main.p","examples/chaos-analysis/lorenz.p","examples/parallel-map-ensembles/main.p"}){const auto path=std::filesystem::path(PENTA_SOURCE_DIR)/relative;std::ifstream stream(path,std::ios::binary);const std::string source((std::istreambuf_iterator<char>(stream)),{});expect(!source.empty(),"category example exists");const auto result=run(source);std::size_t plots=0;for(const auto& action:result.figureActions)plots+=action.kind==FigureActionKind::Plot;expect(plots>0,"category example retains figure samples");}
#endif
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){kernels();
#ifndef PENTA_CHAOS_KERNEL_ONLY
runtime();
#endif
std::cout<<checks<<" chaos/ensemble checks, "<<failures<<" failures\n";return failures?1:0;}
