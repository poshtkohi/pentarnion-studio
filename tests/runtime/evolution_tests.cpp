#include "evolution/evolution_module.hpp"
#ifndef PENTA_EVOLUTION_KERNEL_ONLY
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
#include <thread>
#include <vector>
namespace{
int checks=0,failures=0;void expect(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
struct Context{unsigned objectives=1;bool bad=false,delay=false;};
int objective(void* raw,const double* x,uint64_t n,double* f,uint64_t m,char*,uint64_t){const auto& c=*static_cast<Context*>(raw);if(c.delay)std::this_thread::sleep_for(std::chrono::microseconds(20));if(c.bad){f[0]=std::numeric_limits<double>::quiet_NaN();return 0;}if(m==2){f[0]=x[0]*x[0];f[1]=(x[0]-2)*(x[0]-2);}else{f[0]=0;for(uint64_t j=0;j<n;++j){const double target=j==0?.25:-.5;f[0]+=(x[j]-target)*(x[j]-target);}}return 0;}
penta_evolution_request_v1 request(Context& context,const std::vector<double>& x,const std::vector<double>& lo,const std::vector<double>& hi){penta_evolution_request_v1 q{};q.struct_size=sizeof(q);q.algorithm=1;q.seed=42;q.population=40;q.dimension=x.size();q.objectives=context.objectives;q.max_generations=180;q.max_evaluations=12000;q.history_capacity=200;q.initial=x.data();q.lower=lo.data();q.upper=hi.data();q.maximum_wall_seconds=30;q.ftol=1e-10;q.xtol=1e-8;q.callback_context=&context;q.evaluate=objective;return q;}
template<class F>void rejects(F call,int code,const std::string& label){try{call();expect(false,label);}catch(const penta::evolution::Error& e){expect(e.status==code,label+": "+e.what());}}
void kernels(){using namespace penta::evolution;Module module;expect(!module.loaded(),"evolution starts unloaded");Context context;std::vector<double> x{.9,.8},lower{-1,-1},upper{1,1};auto q=request(context,x,lower,upper);
    for(const uint32_t method:{1u,2u,3u,4u}){q.algorithm=method;const auto result=module.solve(q,1);expect(module.loaded(),"native module loads lazily");expect(result.report.objective_value<1e-5,"known two-dimensional bowl converges: method "+std::to_string(method));expect(std::abs(result.solution[0]-.25)<.004&&std::abs(result.solution[1]+.5)<.004,"physical optimum coordinates: method "+std::to_string(method));expect(result.report.evaluations<=q.max_evaluations&&result.report.history_rows>1,"strict budget and history");for(std::size_t j=0;j<result.populationX.size();++j)expect(result.populationX[j]>=lower[j%2]&&result.populationX[j]<=upper[j%2],"all evaluated survivor coordinates bounded");const auto repeated=module.solve(q,1);expect(result.solution==repeated.solution&&result.populationX==repeated.populationX&&result.populationF==repeated.populationF,"same-build seeded result reproducible");}
    q.algorithm=3;q.restarts=2;q.max_generations=120;q.ftol=1;q.xtol=1;const auto restarted=module.solve(q,1);expect(restarted.report.restarts_completed>=1,"CMA-ES supports population-increasing restart");expect(restarted.report.objective_value<.001,"restart preserves external best");q.restarts=0;
    context.objectives=2;std::vector<double> one{1},lo{0},hi{2};q=request(context,one,lo,hi);q.algorithm=5;q.max_generations=60;const auto pareto=module.solve(q,4);expect(pareto.report.pareto_rows>=30,"NSGA-II retains a distributed known Pareto set");double min=2,max=0;for(uint64_t row=0;row<pareto.report.pareto_rows;++row){const auto value=pareto.paretoX[row];min=std::min(min,value);max=std::max(max,value);expect(std::abs(pareto.paretoF[2*row]-value*value)<1e-12&&std::abs(pareto.paretoF[2*row+1]-(value-2)*(value-2))<1e-12,"Pareto objectives match independent formula");for(uint64_t other=0;other<pareto.report.pareto_rows;++other){const bool dominated=pareto.paretoF[2*other]<=pareto.paretoF[2*row]&&pareto.paretoF[2*other+1]<=pareto.paretoF[2*row+1]&&(pareto.paretoF[2*other]<pareto.paretoF[2*row]||pareto.paretoF[2*other+1]<pareto.paretoF[2*row+1]);expect(!dominated,"returned Pareto front independently nondominated");}}expect(min<.1&&max>1.9,"Pareto front covers both extremes");
    const auto repeatSerial=module.solve(q,1);expect(pareto.populationX==repeatSerial.populationX&&pareto.populationF==repeatSerial.populationF,"NSGA-II deterministic across tested thread limits");
    context.objectives=1;context.delay=true;q=request(context,x,lower,upper);q.algorithm=3;q.max_generations=2;const auto parallel=module.solve(q,4);expect(parallel.statistics.threadsUsed>1&&parallel.statistics.threadsUsed<=4,"CMA-ES batch uses shared budget workers");context.delay=false;
    q=request(context,x,lower,upper);q.max_evaluations=q.population;const auto budget=module.solve(q);expect(budget.report.evaluations==q.population&&!budget.report.converged&&std::string(budget.report.status)=="evaluation_limit","evaluation exhaustion does not imply convergence");
    q.population=9;rejects([&]{(void)module.solve(q);},PENTA_EVOLUTION_ARGUMENT,"invalid population rejected");q=request(context,x,lower,upper);q.lower=nullptr;rejects([&]{(void)module.solve(q);},PENTA_EVOLUTION_ARGUMENT,"missing finite bounds rejected");q=request(context,x,lower,upper);uint8_t logMask[]{1,0};q.log_coordinates=logMask;rejects([&]{(void)module.solve(q);},PENTA_EVOLUTION_ARGUMENT,"nonpositive logarithmic bounds rejected");q=request(context,x,lower,upper);context.bad=true;rejects([&]{(void)module.solve(q);},PENTA_EVOLUTION_DOMAIN,"nonfinite objectives rejected");context.bad=false;
    auto cancel=std::make_shared<std::atomic_bool>(true);rejects([&]{(void)module.solve(q,1,cancel);},PENTA_EVOLUTION_CANCELLED,"pre-cancelled request rejected");
    const auto missing=std::filesystem::temp_directory_path()/("penta-evolution-missing-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(missing);rejects([&]{(void)Module(missing).solve(q);},PENTA_EVOLUTION_INTERNAL,"missing optional module diagnosed");std::filesystem::remove(missing);
}
#ifndef PENTA_EVOLUTION_KERNEL_ONLY
void language(){using namespace penta::runtime;ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-evolution-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache"/"odes";options.maxOperations=100000000;
    auto run=[&](const std::string& source,bool good=true){const auto r=execute(source,{},options);if(good&&!r.ok()){for(const auto& d:r.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:r.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(r.ok()==good,"evolution language: "+source.substr(0,75));};
    run("native function real bowl(real x,real y,real a){return (x-a)^2+(y+0.5)^2;}record r=evolve(bowl,[0,0],options(lower=-1.0,upper=1.0,parameters=options(a=0.25),seed=2,max_generations=100));assert(r.objective<1e-7);record s=evolve(bowl,[0,0],options(lower=-1.0,upper=1.0,parameters=options(a=0.5),seed=2,max_generations=100));assert(s.cache_hit);assert(r.cache_key==s.cache_key);assert(abs(s.x(1)-0.5)<1e-3);");
    run("native function [real a,real b] trade(real x){return x*x,(x-2.0)^2;}record r=evolve(trade,1.0,options(algorithm=\"nsga2\",lower=0.0,upper=2.0,population=40,max_generations=20));assert(size(r.pareto_f,2)==2);assert(size(r.pareto_x,1)>20);");
    run("native function real energy(pentarnion p){return norm2(p-1.0-i);}record r=evolve(energy,0.0,options(lower=-2.0,upper=2.0,population=80,max_generations=150));assert(r.objective<1e-4);");
    run("native function real bowl(real x){return x*x;}record r=evolve(bowl,0.0,options(lower=-1.0,upper=1.0,typo=1));",false);
    run("native function real bowl(real x){return x*x;}record r=evolve(bowl,0.0,options(algorithm=\"nsga2\",lower=-1.0,upper=1.0));",false);
#ifdef PENTA_SOURCE_DIR
    std::ifstream stream(std::filesystem::path(PENTA_SOURCE_DIR)/"examples/evolutionary-optimization/main.p",std::ios::binary);const std::string source((std::istreambuf_iterator<char>(stream)),{});expect(!source.empty(),"evolutionary-optimization example exists");if(!source.empty())run(source);
#endif
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){kernels();
#ifndef PENTA_EVOLUTION_KERNEL_ONLY
language();
#endif
std::cout<<checks<<" evolutionary checks, "<<failures<<" failures\n";return failures?1:0;}
