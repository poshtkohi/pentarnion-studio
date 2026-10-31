#include "roots/root_module.hpp"
#ifndef PENTA_ROOT_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <atomic>
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
void expect(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
bool close(double a,double b,double tol=1e-8){return std::abs(a-b)<tol*(1+std::abs(b));}
struct Context {int kind=0;std::shared_ptr<std::atomic_bool> cancellation;};
int callback(void* raw,const double* x,uint64_t n,double* f,double* j,char* error,uint64_t capacity){
    auto& context=*static_cast<Context*>(raw);
    if(context.kind==1){f[0]=x[0]*x[0]+x[1]*x[1]-2;f[1]=x[0]-x[1];if(j){j[0]=2*x[0];j[1]=2*x[1];j[2]=1;j[3]=-1;}}
    else if(context.kind==2){for(uint64_t i=0;i<n;++i){f[i]=.5*x[i]+static_cast<double>(i+1);if(j)for(uint64_t k=0;k<n;++k)j[i*n+k]=i==k?.5:0;}}
    else if(context.kind==3){f[0]=x[0]*x[0]+1;if(j)j[0]=2*x[0];}
    else if(context.kind==4){f[0]=x[0]<0?-1:1;if(j)j[0]=0;}
    else if(context.kind==5){if(capacity){const std::string text="native callback domain failure";const auto count=std::min<uint64_t>(text.size(),capacity-1);std::copy_n(text.data(),count,error);error[count]=0;}return PENTA_ROOTS_CALLBACK;}
    else if(context.kind==6){f[0]=std::numeric_limits<double>::quiet_NaN();}
    else if(context.kind==7){f[0]=x[0]-2;if(j)j[0]=1;}
    else{f[0]=x[0]*x[0]-2;if(j)j[0]=2*x[0];}
    if(context.cancellation)context.cancellation->store(true);
    return 0;
}
penta_roots_request_v1 request(Context& context,uint64_t n=1){penta_roots_request_v1 r{};r.struct_size=sizeof(r);r.operation=PENTA_ROOTS_SCALAR;r.dimension=n;r.max_iterations=200;r.max_evaluations=10000;r.line_search=1;r.absolute_tolerance=1e-12;r.relative_tolerance=1e-12;r.residual_tolerance=1e-10;r.maximum_wall_seconds=60;r.lower=0;r.upper=2;r.callback_context=&context;r.evaluate=callback;return r;}
template<class F>void rejects(F task,int status,const std::string& label){try{task();expect(false,label);}catch(const penta::roots::Error& error){expect(error.status==status,label+": "+error.what());}}
void kernels(){
    using namespace penta::roots;
    Module module;expect(!module.loaded(),"root module starts unloaded");Context context;
    auto r=request(context);const auto scalar=module.solve(r);expect(module.loaded(),"first root use lazily loads module");expect(scalar.report.converged&&close(scalar.solution[0],std::sqrt(2.)),"TOMS748 square root");expect(scalar.report.residual_norm<=1e-10,"scalar residual independently checked");expect(scalar.report.lower<=std::sqrt(2.)&&scalar.report.upper>=std::sqrt(2.),"bracket contains root");expect(scalar.report.evaluations>=3,"scalar evaluation count includes endpoints and verification");
    context.kind=7;r.lower=2;r.upper=3;auto endpoint=module.solve(r);expect(endpoint.report.converged&&endpoint.solution[0]==2,"exact endpoint root converges");context.kind=0;
    r=request(context);r.lower=2;r.upper=3;rejects([&]{(void)module.solve(r);},PENTA_ROOTS_ARGUMENT,"same-sign bracket rejected");
    r=request(context);r.lower=r.upper;rejects([&]{(void)module.solve(r);},PENTA_ROOTS_ARGUMENT,"zero-width bracket rejected");
    r=request(context);r.lower=-std::numeric_limits<double>::max();r.upper=std::numeric_limits<double>::max();rejects([&]{(void)module.solve(r);},PENTA_ROOTS_ARGUMENT,"unrepresentable bracket width rejected");
    r=request(context);r.max_evaluations=3;const auto limited=module.solve(r);expect(!limited.report.converged&&limited.report.evaluations==3,"small evaluation budget gives bounded non-root report");
    context.kind=4;r=request(context);r.lower=-1;r.upper=1;const auto discontinuous=module.solve(r);expect(!discontinuous.report.converged&&discontinuous.report.residual_norm==1,"collapsed discontinuous bracket is not declared a root");
    context.kind=1;std::vector<double> initial{.8,.5},scale{1,1};r=request(context,2);r.operation=PENTA_ROOTS_SYSTEM;r.initial=initial.data();r.variable_scale=scale.data();r.residual_scale=scale.data();const auto analytic=module.solve(r);expect(analytic.report.converged&&close(analytic.solution[0],1)&&close(analytic.solution[1],1),"KINSOL generated Jacobian system");expect(analytic.report.jacobian_evaluations>0&&analytic.report.iterations>0,"KINSOL statistics captured");expect(analytic.report.scaled_residual_norm<=r.residual_tolerance,"scaled residual confirms convergence");
    r.finite_difference=1;const auto finiteDifference=module.solve(r);expect(finiteDifference.report.converged&&close(finiteDifference.solution[0],1)&&close(finiteDifference.solution[1],1),"KINSOL internal finite difference system");
    r.line_search=0;const auto newton=module.solve(r);expect(newton.report.converged,"plain Newton option");
    r.max_iterations=1;const auto oneStep=module.solve(r);expect(!oneStep.report.converged&&oneStep.report.iterations<=1,"iteration exhaustion is not convergence");
    context.kind=2;std::vector<double> pentInitial(5,0);r=request(context,5);r.operation=PENTA_ROOTS_FIXEDPOINT;r.initial=pentInitial.data();const auto map=module.solve(r);if(!map.report.converged){std::cerr<<"Map flag="<<map.report.backend_flag<<" status="<<map.report.status<<" residual="<<map.report.residual_norm<<" x:";for(const auto value:map.solution)std::cerr<<' '<<value;std::cerr<<'\n';}expect(map.report.converged,"five-coordinate fixed point");for(std::size_t n=0;n<5;++n)expect(close(map.solution[n],2*static_cast<double>(n+1)),"fixed point subtracts x and identity Jacobian");
    context.kind=3;double zero=0;r=request(context);r.operation=PENTA_ROOTS_SYSTEM;r.initial=&zero;const auto absent=module.solve(r);expect(!absent.report.converged&&absent.report.residual_norm>=1,"singular no-root system reports failure");
    context.kind=5;r=request(context);rejects([&]{(void)module.solve(r);},PENTA_ROOTS_CALLBACK,"native callback failure preserved");context.kind=6;rejects([&]{(void)module.solve(r);},PENTA_ROOTS_DOMAIN,"nonfinite native residual rejected");
    context.kind=0;r=request(context);auto cancellation=std::make_shared<std::atomic_bool>(true);rejects([&]{(void)module.solve(r,cancellation);},PENTA_ROOTS_CANCELLED,"pre-cancelled root rejected");cancellation->store(false);context.cancellation=cancellation;rejects([&]{(void)module.solve(r,cancellation);},PENTA_ROOTS_CANCELLED,"active root cancellation");context.cancellation.reset();cancellation->store(false);expect(module.solve(r,cancellation).report.converged,"module reusable after cancellation");
    struct BudgetFailure:std::exception{};bool preserved=false;try{(void)module.solve(r,{},[](std::size_t){throw BudgetFailure{};});}catch(const BudgetFailure&){preserved=true;}expect(preserved,"host budget exception identity preserved over C ABI");
    double invalidScale=-1;r.residual_scale=&invalidScale;rejects([&]{(void)module.solve(r);},PENTA_ROOTS_ARGUMENT,"negative residual scale rejected");
    r=request(context);r.maximum_wall_seconds=0;rejects([&]{(void)module.solve(r);},PENTA_ROOTS_ARGUMENT,"zero wall budget rejected");
    const auto absentPath=std::filesystem::temp_directory_path()/("penta-root-module-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(absentPath);Module missing(absentPath);r=request(context);rejects([&]{(void)missing.solve(r);},PENTA_ROOTS_INTERNAL,"missing optional root module diagnosed");std::filesystem::remove(absentPath);
    rejects([&]{Module relative("relative");},PENTA_ROOTS_ARGUMENT,"relative roots module location rejected");
#ifdef PENTA_BAD_ROOT_MODULE_DIR
    Module incompatible(std::filesystem::path(PENTA_BAD_ROOT_MODULE_DIR));rejects([&]{(void)incompatible.solve(r);},PENTA_ROOTS_VERSION,"incompatible root module ABI rejected");expect(!incompatible.loaded(),"incompatible root module is not cached");
#endif
}
#ifndef PENTA_ROOT_KERNEL_ONLY
void runtime(){
    using namespace penta::runtime;
    ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-root-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache"/"odes";
    auto run=[&](const std::string& source,bool good=true){const auto result=execute(source,{},options);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(result.ok()==good,"runtime root program "+source.substr(0,65));return result;};
    run("native function real F(real x,real a){return x*x-a;}record r=root(F,[0,2],options(parameters=options(a=2.0)));assert(r.converged);assert(abs(r.x-sqrt(2))<1e-9);assert(r.residual_norm<1e-9);assert(r.threads_used==1);record q=root(F,[0,3],options(parameters=options(a=3.0)));assert(q.cache_hit);assert(abs(q.x-sqrt(3))<1e-9);");
    run("native function [real a,real b] F(real x,real y){return x*x+y*y-2.0,x-y;}record r=rootsolve(F,[0.8,0.5]);assert(r.converged);assert(norm(r.x-[1,1])<1e-8);assert(r.native_evaluations>r.evaluations);record q=rootsolve(F,[0.8,0.5],options(jacobian=\"finite_difference\"));assert(q.converged);assert(q.jacobian_evaluations>0);assert(q.native_evaluations==q.evaluations);");
    run("native function pentarnion F(pentarnion p,pentarnion a,pentarnion b){return a*p-b;}pentarnion target=1+i-j+0.25*ell;pentarnion a=2+0.25*j;pentarnion b=a*target;record r=rootsolve(F,0.0,options(parameters=options(a=a,b=b)));assert(r.converged);assert(abs(coeff(r.x-target,0))<1e-8);assert(abs(coeff(r.x-target,1))<1e-8);assert(abs(coeff(r.x-target,2))<1e-8);assert(abs(coeff(r.x-target,3))<1e-8);assert(abs(coeff(r.x-target,4))<1e-8);");
    run("native function pentarnion G(pentarnion p,pentarnion a){return 0.5*p+a;}record r=fixedpoint(G,i,options(parameters=options(a=1.0+j)));assert(r.converged);assert(abs(coeff(r.x,0)-2)<1e-8);assert(abs(coeff(r.x,2)-2)<1e-8);");
    run("ode decay(t,real y;real rate,real forcing){y'=-rate*y+forcing;}record r=equilibrium(decay,0.0,options(parameters=options(rate=2.0,forcing=6.0)));assert(r.converged);assert(abs(r.x-3)<1e-9);");
    run("ode forced(t,real y){y'=t-y;}record r=equilibrium(forced,0.0);",false);
    run("native function real F(real x){return x*x+1.0;}record r=rootsolve(F,0.0);assert(!r.converged);assert(r.residual_norm>=1);");
    run("native function real F(real x){return x*x-2.0;}record r=root(F,[2,3]);",false);
    run("native function real F(real x){return x*x-2.0;}record r=root(F,[0,2],options(typo=1));",false);
    run("native function real F(real x){return x*x-2.0;}record r=rootsolve(F,1.0,options(parameters=options(unused=2.0)));",false);
    run("native function real F(real x){return x*x-2.0;}record r=rootsolve(F,[1,2]);",false);
    run("native function [real a,real b] F(real x,pentarnion p){return x,x;}record r=rootsolve(F,(1.0,i));",false);
    run("native function real F(real x){return x*x-2.0;}record r=rootsolve(F,1.0,options(threads=-1));",false);
    const auto cancelled=std::make_shared<std::atomic_bool>(true);auto cancelledOptions=options;cancelledOptions.cancellation=cancelled;expect(execute("real x=1;",{},cancelledOptions).status==ExecutionStatus::Cancelled,"root integration preserves cancellation status");
#ifdef PENTA_SOURCE_DIR
    const auto source=std::filesystem::path(PENTA_SOURCE_DIR)/"examples/root-finding/main.p";std::ifstream stream(source,std::ios::binary);const std::string example((std::istreambuf_iterator<char>(stream)),{});expect(!example.empty(),"root-finding example source exists");const auto exampleResult=run(example);std::size_t plots=0,points=0,vectors=0;for(const auto& action:exampleResult.figureActions)if(action.kind==FigureActionKind::Plot){++plots;vectors+=action.vectors.size();for(const auto& curve:action.curves)points+=curve.points.size();}expect(plots==2,"root-finding retains two subplot plots");expect(points==201&&vectors==3,"root-finding retains residual samples and ordered Pentarnion vectors");
#endif
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){kernels();
#ifndef PENTA_ROOT_KERNEL_ONLY
runtime();
#endif
std::cout<<checks<<" root checks, "<<failures<<" failures\n";return failures?1:0;}

