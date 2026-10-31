#include "optimization/opt_module.hpp"
#ifndef PENTA_OPTIMIZATION_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
namespace {
int checks=0,failures=0;
void expect(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
bool close(double a,double b,double tolerance=1e-6){return std::abs(a-b)<=tolerance*(1+std::abs(b));}
struct Context{int kind=0;std::shared_ptr<std::atomic_bool> cancellation;};
int objective(void* opaque,const double* x,uint64_t n,double* f,uint64_t,double* j,char*,uint64_t){const auto& c=*static_cast<Context*>(opaque);f[0]=0;for(uint64_t index=0;index<n;++index){const double target=c.kind==1?2:c.kind==2?.25:index==0?2:-1;f[0]+=(x[index]-target)*(x[index]-target);if(j)j[index]=2*(x[index]-target);}if(c.kind==3)f[0]=std::numeric_limits<double>::quiet_NaN();if(c.cancellation)c.cancellation->store(true);return 0;}
int inequality(void*,const double* x,uint64_t,double* f,uint64_t,double* j,char*,uint64_t){f[0]=x[0]+x[1]-1;if(j){j[0]=j[1]=1;}return 0;}
int equality(void*,const double* x,uint64_t,double* f,uint64_t,double* j,char*,uint64_t){f[0]=x[0]-x[1];if(j){j[0]=1;j[1]=-1;}return 0;}
penta_opt_request_v1 request(Context& context,const std::vector<double>& initial){penta_opt_request_v1 q{};q.struct_size=sizeof(q);q.algorithm=1;q.seed=42;q.dimension=initial.size();q.initial=initial.data();q.max_evaluations=10000;q.history_capacity=10000;q.ftol_relative=1e-12;q.ftol_absolute=1e-14;q.xtol_relative=q.xtol_absolute=1e-12;q.maximum_wall_seconds=30;q.constraint_tolerance=1e-8;q.objective_context=&context;q.objective=objective;return q;}
template<class F>void rejects(F task,int status,const std::string& label){try{task();expect(false,label);}catch(const penta::optimization::Error& value){expect(value.status==status,label+": "+value.what());}}
void kernels(){
    using namespace penta::optimization;Module module;expect(!module.loaded(),"optimization module starts unloaded");Context context;std::vector<double> initial{0,0};auto q=request(context,initial);const auto gradient=module.solve(q);expect(module.loaded(),"NLopt loads on demand");expect(gradient.report.converged&&close(gradient.solution[0],2)&&close(gradient.solution[1],-1),"LBFGS independently known optimum");expect(gradient.report.gradient_evaluations>0&&gradient.report.history_rows>0,"native gradient and objective histories retained");
    for(uint32_t method:{2u,3u}){q.algorithm=method;q.ftol_relative=q.ftol_absolute=0;const auto free=module.solve(q);if(!free.report.converged||!close(free.solution[0],2,1e-5)||!close(free.solution[1],-1,1e-5))std::cerr<<"Local method="<<method<<" x="<<free.solution[0]<<','<<free.solution[1]<<" flag="<<free.report.backend_flag<<" status="<<free.report.status<<'\n';expect(close(free.solution[0],2,1e-5)&&close(free.solution[1],-1,1e-5),"derivative-free local independent optimum");expect(free.report.converged||free.report.backend_flag==-4,"local result distinguishes convergence from roundoff limit");expect(free.report.gradient_evaluations==0,"derivative-free method does not request gradient");}
    context.kind=1;q=request(context,initial);q.algorithm=4;q.inequality_count=1;q.inequality=inequality;q.equality_count=1;q.equality=equality;const auto constrained=module.solve(q);expect(constrained.report.converged&&constrained.report.feasible&&close(constrained.solution[0],.5)&&close(constrained.solution[1],.5),"SLSQP native inequalities/equality optimum");expect(constrained.report.constraint_violation<=q.constraint_tolerance,"final feasibility independently evaluated");
    q.algorithm=5;const auto cobyla=module.solve(q);expect(cobyla.report.feasible&&close(cobyla.solution[0],.5,1e-4)&&close(cobyla.solution[1],.5,1e-4),"COBYLA native real constraints");expect(cobyla.report.gradient_evaluations==0,"COBYLA never requests derivatives");
    context.kind=0;q=request(context,initial);std::vector<double> scale{100,.1};q.parameter_scale=scale.data();const auto scaled=module.solve(q);expect(scaled.report.converged&&close(scaled.solution[0],2)&&close(scaled.solution[1],-1),"physical parameter scaling preserves solution");
    context.kind=2;std::vector<double> scalar{.9},lower{-1},upper{1};q=request(context,scalar);q.lower=lower.data();q.upper=upper.data();q.max_evaluations=1000;
    for(uint32_t method:{6u,7u,8u}){q.algorithm=method;const auto global=module.solve(q);expect(global.report.feasible&&global.report.objective_value<1e-4,"finite global search finds known scalar neighborhood");expect(global.report.evaluations<=q.max_evaluations,"global native evaluation budget is strict");if(method==7){const auto repeat=module.solve(q);expect(global.solution==repeat.solution&&global.report.objective_value==repeat.report.objective_value,"seeded CRS search is reproducible");}}
    q.algorithm=1;q.max_evaluations=1;const auto limited=module.solve(q);expect(!limited.report.converged&&limited.report.evaluations==1,"evaluation exhaustion is not convergence");
    q=request(context,scalar);q.algorithm=6;rejects([&]{(void)module.solve(q);},PENTA_OPT_ARGUMENT,"global missing finite bounds rejected");q.algorithm=1;q.inequality_count=1;q.inequality=inequality;rejects([&]{(void)module.solve(q);},PENTA_OPT_ARGUMENT,"unsupported constraint algorithm rejected");
    context.kind=3;q=request(context,initial);rejects([&]{(void)module.solve(q);},PENTA_OPT_DOMAIN,"nonfinite objective rejected");context.kind=0;
    auto cancellation=std::make_shared<std::atomic_bool>(true);rejects([&]{(void)module.solve(q,cancellation);},PENTA_OPT_CANCELLED,"pre-cancelled optimization rejected");cancellation->store(false);context.cancellation=cancellation;rejects([&]{(void)module.solve(q,cancellation);},PENTA_OPT_CANCELLED,"native evaluation cancellation");context.cancellation.reset();
    const auto missing=std::filesystem::temp_directory_path()/("penta-opt-missing-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(missing);rejects([&]{(void)Module(missing).solve(q);},PENTA_OPT_INTERNAL,"missing optional optimization module diagnosed");std::filesystem::remove(missing);
}
#ifndef PENTA_OPTIMIZATION_KERNEL_ONLY
void runtime(){
    using namespace penta::runtime;ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-opt-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache"/"odes";
    auto run=[&](const std::string& source,bool good=true){const auto result=execute(source,{},options);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(result.ok()==good,"optimization language: "+source.substr(0,65));return result;};
    run("native function real bowl(real x,real y,real a){return (x-a)*(x-a)+(y+1.0)*(y+1.0);}record r=minimize(bowl,[0,0],options(parameters=options(a=2.0)));assert(r.converged);assert(norm(r.x-[2,-1])<1e-7);record s=minimize(bowl,[0,0],options(parameters=options(a=3.0),jacobian=\"finite_difference\"));assert(s.cache_hit);assert(norm(s.x-[3,-1])<1e-6);");
    run("native function real objective(real x,real y){return (x-2.0)*(x-2.0)+(y-2.0)*(y-2.0);}native function real g(real x,real y){return x+y-1.0;}native function real h(real x,real y){return x-y;}threads(3);record r=minimize(objective,[0,0],options(algorithm=\"slsqp\",inequality=g,equality=h,threads=1));assert(r.converged);assert(r.feasible);assert(norm(r.x-[0.5,0.5])<1e-6);assert(threads()==3);");
    run("native function real energy(pentarnion p){return coeff(p-1.0-i,0)^2+coeff(p-1.0-i,1)^2+coeff(p,2)^2+coeff(p,3)^2+coeff(p,4)^2;}record r=minimize(energy,0.0);assert(r.converged);assert(abs(coeff(r.x,0)-1)<1e-6);assert(abs(coeff(r.x,1)-1)<1e-6);assert(abs(coeff(r.x,2))<1e-6);");
    run("native function real objective(real x){return x*x;}record r=minimize(objective,1.0,options(algorithm=\"direct\"));",false);
    run("native function real objective(real x){return x*x;}record r=minimize(objective,1.0,options(typo=1));",false);
    run("native function pentarnion objective(pentarnion p){return p*p;}record r=minimize(objective,1.0);",false);
    run("native function real objective(real x){return x*x;}record r=minimize(objective,1.0,options(max_evaluations=1));assert(!r.converged);assert(r.evaluations==1);");
    run("native function real objective(real x){if(x<0.0){return sqrt(x);}return (x-1.0)*(x-1.0);}record r=minimize(objective,0.0,options(lower=0.0,upper=2.0,jacobian=\"finite_difference\",history_limit=0));assert(r.converged);assert(abs(r.x-1.0)<1e-7);assert(numel(r.history)==0);");
    run("native function real objective(real x){return x*x;}record r=minimize(objective,1.0,options(history_limit=100000));",false);
#ifdef PENTA_SOURCE_DIR
    std::ifstream stream(std::filesystem::path(PENTA_SOURCE_DIR)/"examples/optimization/main.p",std::ios::binary);const std::string source((std::istreambuf_iterator<char>(stream)),{});expect(!source.empty(),"optimization example exists");if(!source.empty())run(source);
#endif
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){kernels();
#ifndef PENTA_OPTIMIZATION_KERNEL_ONLY
runtime();
#endif
std::cout<<checks<<" optimization checks, "<<failures<<" failures\n";return failures?1:0;}
