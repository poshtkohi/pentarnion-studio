#include "sensitivity/sensitivity_module.hpp"
#ifndef PENTA_SENSITIVITY_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0;
void require(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
template<class F>void rejects(F&& f,const std::string& label){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,label);}
struct Model {
    unsigned kind=0;double offset=0;bool fail=false;std::atomic_uint64_t calls{0};std::shared_ptr<std::atomic_bool> flag;
    static int evaluate(void* raw,const double* x,uint64_t,double* value,char*,uint64_t){auto& self=*static_cast<Model*>(raw);if(self.calls.fetch_add(1)>100&&self.flag)self.flag->store(true);if(self.fail)return 1;*value=self.offset+(self.kind==0?x[0]+2*x[1]:self.kind==1?x[0]*x[1]:self.kind==2?std::sin(x[0])+7*std::pow(std::sin(x[1]),2)+.1*std::pow(x[2],4)*std::sin(x[0]):0);return 0;}
};
struct Problem {
    std::vector<double> lower,upper;penta_sensitivity_request_v1 request{};
    Problem(Model& model,unsigned d=2):lower(d,0),upper(d,1){auto& r=request;r.struct_size=sizeof(r);r.method=PENTA_SENS_SOBOL;r.dimensions=d;r.levels=6;r.samples=16384;r.seed=123;r.max_evaluations=8000000;r.max_wall_seconds=30;r.confidence=.95;r.lower=lower.data();r.upper=upper.data();r.callback_context=&model;r.evaluate=Model::evaluate;}
};
void nativeTests(){
    penta::sensitivity::Module module;
    {Model model;Problem problem(model);problem.request.method=0;problem.request.samples=4096;problem.lower={-1,0};problem.upper={2,4};problem.request.lower=problem.lower.data();problem.request.upper=problem.upper.data();const auto a=module.analyze(problem.request,{},1),b=module.analyze(problem.request,{},4);require(a.mu==b.mu&&a.muStar==b.muStar&&a.sigma==b.sigma,"Morris serial/parallel deterministic results");require(std::abs(a.mu[0]-3)<1e-13&&std::abs(a.mu[1]-8)<1e-13,"Morris effect uses normalized input range");require(a.sigma[0]<1e-13&&a.sigma[1]<1e-13,"linear Morris effects constant");require(a.report.evaluations==12288,"Morris trajectory evaluation count");require(b.statistics.threadsUsed>1&&b.statistics.threadsUsed<=4,"Morris actual shared-budget workers");}
    {Model model;Problem problem(model);problem.request.samples=65536;const auto a=module.analyze(problem.request,{},1),b=module.analyze(problem.request,{},4);require(a.first==b.first&&a.total==b.total,"Sobol serial/parallel exact reproducibility");require(std::abs(a.first[0]-.2)<.015&&std::abs(a.first[1]-.8)<.015,"linear independent analytic first-order Sobol");require(std::abs(a.total[0]-.2)<.015&&std::abs(a.total[1]-.8)<.015,"linear independent analytic total-order Sobol");require(std::abs(a.report.mean-1.5)<.01&&std::abs(a.report.variance-5.0/12)<.01,"linear mean and variance");require(a.report.evaluations==65536*4,"Sobol matched callback count");require(b.statistics.threadsUsed>1&&b.statistics.threadsUsed<=4,"Sobol actual bounded parallel execution");model.offset=1000000;const auto shifted=module.analyze(problem.request,{},4);require(std::abs(shifted.first[0]-a.first[0])<1e-9&&std::abs(shifted.total[1]-a.total[1])<1e-9,"constant-offset stable estimates");}
    {Model model;model.kind=1;Problem problem(model);problem.request.samples=65536;const auto result=module.analyze(problem.request,{},4);for(unsigned j=0;j<2;++j){require(std::abs(result.first[j]-3.0/7)<.015,"product model first-order interaction reference");require(std::abs(result.total[j]-4.0/7)<.015,"product model total-order interaction reference");}problem.request.method=0;problem.request.samples=256;const auto morris=module.analyze(problem.request,{},4);require(morris.sigma[0]>.1&&morris.sigma[1]>.1,"Morris interaction dispersion");}
    {Model model;model.kind=2;Problem problem(model,3);std::fill(problem.lower.begin(),problem.lower.end(),-std::numbers::pi);std::fill(problem.upper.begin(),problem.upper.end(),std::numbers::pi);problem.request.samples=65536;const auto result=module.analyze(problem.request,{},4);const double p4=std::pow(std::numbers::pi,4),variance=.5+49.0/8+.1*p4/5+.01*p4*p4/18,v1=.5*std::pow(1+.1*p4/5,2),v2=49.0/8;const double first[]={v1/variance,v2/variance,0},total[]={(variance-v2)/variance,v2/variance,(variance-v1-v2)/variance};for(unsigned j=0;j<3;++j){require(std::abs(result.first[j]-first[j])<.02,"Ishigami independently integrated first-order");require(std::abs(result.total[j]-total[j])<.02,"Ishigami independently integrated total-order");}}
    {Model model;Problem problem(model);problem.request.samples=1024;problem.request.bootstrap=64;const auto a=module.analyze(problem.request,{},1),b=module.analyze(problem.request,{},4);require(a.firstLower==b.firstLower&&a.totalUpper==b.totalUpper,"matched-row bootstrap deterministic across worker schedules");for(unsigned j=0;j<2;++j){require(a.firstLower[j]<a.firstUpper[j]&&a.totalLower[j]<a.totalUpper[j],"finite-sample bootstrap interval width");require(std::isfinite(a.firstLower[j])&&std::isfinite(a.totalUpper[j]),"bootstrap finite estimates");}require(a.report.evaluations==4096,"bootstrap reuses stored model evaluations");}
    {Model model;Problem problem(model);problem.request.samples=16;problem.request.method=0;problem.request.levels=5;rejects([&]{module.analyze(problem.request,{},4);},"odd Morris levels rejected");problem.request.levels=6;problem.upper[0]=0;rejects([&]{module.analyze(problem.request,{},4);},"degenerate input range rejected");problem.upper[0]=1;problem.request.max_evaluations=1;rejects([&]{module.analyze(problem.request,{},4);},"evaluation budget rejected before native calls");problem.request.max_evaluations=100000;model.fail=true;rejects([&]{module.analyze(problem.request,{},4);},"native model failure joins workers");model.fail=false;require(module.analyze(problem.request,{},4).report.evaluations==48,"worker pool reusable after failure");model.kind=3;problem.request.method=1;rejects([&]{module.analyze(problem.request,{},4);},"zero-variance Sobol rejected");model.kind=0;problem.request.samples=1024;auto flag=std::make_shared<std::atomic_bool>(false);model.calls=0;model.flag=flag;rejects([&]{module.analyze(problem.request,flag,4);},"sensitivity cooperative cancellation");model.flag.reset();require(module.analyze(problem.request,{},4).report.evaluations==4096,"worker pool reusable after cancellation");}
    {
        Model model;Problem problem(model);problem.request.samples=16;
        struct Nested{penta::sensitivity::Module& module;penta_sensitivity_request_v1& request;std::atomic_uint64_t correct{0};
            static int run(void* raw,uint64_t begin,uint64_t end,char*,uint64_t)noexcept{auto& self=*static_cast<Nested*>(raw);try{for(uint64_t i=begin;i<end;++i){const auto r=self.module.analyze(self.request,{},4);if(r.threadsGranted==1&&r.statistics.threadsUsed==1)self.correct.fetch_add(1);}return 0;}catch(...){return 4;}}
        }nested{module,problem.request};penta::runtime::ParallelSettings settings;settings.threads=4;settings.serialThreshold=1;penta::runtime::ParallelStatistics observed;std::string error;
        const auto status=penta::runtime::ThreadBudget::instance().run(4,4096,Nested::run,&nested,settings,{},observed,error);require(status==0&&nested.correct.load()==4,"native nested callers inherit one shared CPU slot without deadlock");
    }
}
#ifndef PENTA_SENSITIVITY_KERNEL_ONLY
void languageTests(){
    penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-sensitivity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";
    auto run=[&](const std::string& source,bool good=true){const auto result=penta::runtime::execute(source,{},options);require(result.ok()==good,"native sensitivity language case");if(result.ok()!=good){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}};
    const std::string model="native function real linear(real x,real y,real scale){return scale*(x+2*y);}";
    run(model+"record a=sensitivity(linear,[0,0],[1,1],options(parameters=options(scale=1.0),method=\"morris\",samples=64,seed=42,threads=4));assert(abs(a.mu(1)-1)<1e-12&&abs(a.mu(2)-2)<1e-12);record b=sensitivity(linear,[0,0],[1,1],options(parameters=options(scale=2.0),method=\"sobol\",samples=1024,seed=42,bootstrap=32,threads=4));assert(a.cache_key==b.cache_key&&b.cache_hit);assert(b.distribution==\"independent_uniform\"&&b.uncertainty==\"matched_row_percentile_bootstrap\");assert(abs(b.first_order(1)-.2)<.1);assert(b.evaluations==4096);");
    run("native function real model(pentarnion p){return coeff(p,0)+2*coeff(p,1)+3*coeff(p,2)+4*coeff(p,3)+5*coeff(p,4);}record a=sensitivity(model,[0,0,0,0,0],[1,1,1,1,1],options(method=\"morris\",samples=32,threads=4));assert(norm(a.mu-[1,2,3,4,5],2)<1e-10);");
    run(model+"record a=sensitivity(linear,[0,0],[1,1],options(parameters=options(scale=1.0),method=\"sobol\",samples=64,max_native_operations=1,threads=4));",false);
    run(model+"record a=sensitivity(linear,[0,0],[1,1],options(parameters=options(scale=1.0),method=\"morris\",levels=5));",false);
    run(model+"record a=sensitivity(linear,[0,0],[1,1],options(parameters=options(scale=1.0),method=\"sobol\",threads=-1));",false);
    run("native function pentarnion model(real x){return x*i;}record a=sensitivity(model,[0],[1]);",false);
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){try{nativeTests();
#ifndef PENTA_SENSITIVITY_KERNEL_ONLY
languageTests();
#endif
}catch(const std::exception& failure){++failures;std::cerr<<"UNEXPECTED: "<<failure.what()<<'\n';}std::cout<<checks<<" sensitivity checks, "<<failures<<" failures\n";return failures?1:0;}
