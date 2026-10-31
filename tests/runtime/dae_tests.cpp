#include "dae/dae_module.hpp"
#ifndef PENTA_DAE_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0;
void require(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
template<class F>void rejects(F&& f,const std::string& label){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,label);}
struct Model {
    unsigned kind=0,n=1;double rate=2,restitution=.8;std::atomic_uint64_t calls{0};std::shared_ptr<std::atomic_bool> cancellation;
    static int residual(void* raw,double t,const double* y,const double* yp,double* f,double* jy,double* jyp,char*,uint64_t){auto& m=*static_cast<Model*>(raw);if(m.calls.fetch_add(1)>3&&m.cancellation)m.cancellation->store(true);if(jy){std::fill_n(jy,m.n*m.n,0);std::fill_n(jyp,m.n*m.n,0);}
        if(m.kind==0){for(unsigned j=0;j<m.n;++j){f[j]=yp[j]+m.rate*y[j];if(jy){jy[j*m.n+j]=m.rate;jyp[j*m.n+j]=1;}}}
        else if(m.kind==1){f[0]=yp[0]+y[0]-y[1];f[1]=y[1]-std::sin(t);if(jy){jy[0]=1;jy[1]=-1;jy[3]=1;jyp[0]=1;}}
        else if(m.kind==2){f[0]=(1+y[0]*y[0])*yp[0]+y[0];if(jy){jy[0]=2*y[0]*yp[0]+1;jyp[0]=1+y[0]*y[0];}}
        else if(m.kind==3){f[0]=yp[0]-y[1];f[1]=y[0];if(jy){jyp[0]=1;jy[1]=-1;jy[2]=1;}}
        else if(m.kind==4){f[0]=y[0]-std::sin(t);if(jy)jy[0]=1;}
        else{f[0]=yp[0]-y[1];f[1]=yp[1]+1;if(jy){jy[1]=-1;jyp[0]=jyp[3]=1;}}return 0;
    }
    static int event(void*,double,const double* y,const double*,double* out,char*,uint64_t){*out=y[0];return 0;}
    static int reset(void* raw,double,const double* y,const double*,double* out,double* derivative,char*,uint64_t){const auto& m=*static_cast<Model*>(raw);out[0]=0;out[1]=-m.restitution*y[1];derivative[0]=out[1];derivative[1]=-1;return 0;}
};
struct Problem {
    std::vector<double> initial,derivative;std::vector<uint8_t> algebraic;penta_dae_request_v1 request{};
    Problem(Model& model,unsigned n=1):initial(n,1),derivative(n,-2),algebraic(n,0){model.n=n;auto& r=request;r.struct_size=sizeof(r);r.dimension=n;r.initial=initial.data();r.initial_derivative=derivative.data();r.algebraic=algebraic.data();r.t1=1;r.sample_step=.05;r.rtol=1e-9;r.atol=1e-11;r.initial_residual_tolerance=1e-8;r.index_tolerance=1e-12;r.max_wall_seconds=30;r.max_steps=100000;r.max_evaluations=1000000;r.max_events=100;r.minimum_event_interval=1e-8;r.callback_context=&model;r.residual=Model::residual;}
};
void nativeTests(){
    penta::dae::Module module;
    for(const unsigned jac:{0u,1u}){Model model;Problem p(model);p.request.jacobian=jac;const auto s=module.solve(p.request,{},[](std::size_t){});require(std::abs(s.values.back()-std::exp(-2))<2e-8,"IDAS real decay analytic/finite-difference solver Jacobian");require(s.report.samples==21&&s.times.back()==1,"DAE sample grid");require(s.initial[0]==1&&s.initialDerivative[0]==-2,"check mode preserves initial values");require(s.report.initial_correction_norm==0&&s.report.maximum_sample_residual<1e-6,"DAE residual diagnostics");}
    {Model model;model.kind=1;Problem p(model,2);p.initial[0]=p.initial[1]=0;p.derivative[0]=0;p.derivative[1]=1;p.algebraic[1]=1;const auto s=module.solve(p.request,{},[](std::size_t){});require(std::abs(s.values[s.values.size()-2]-.5*(std::sin(1)-std::cos(1)+std::exp(-1)))<2e-8,"index-one forced differential solution");require(std::abs(s.values.back()-std::sin(1))<2e-8,"index-one algebraic constraint");p.initial[1]=.3;p.derivative[0]=.7;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"inconsistent check mode rejects");p.request.initialization=1;const auto corrected=module.solve(p.request,{},[](std::size_t){});require(std::abs(corrected.initial[1])<1e-8&&std::abs(corrected.initialDerivative[0])<1e-8,"IDACalcIC corrects algebraic state/differential derivative");require(corrected.initial[0]==0&&corrected.initialDerivative[1]==1,"consistent mode fixes declared differential states/algebraic derivatives");require(corrected.report.initial_correction_norm>.6,"initial correction is reported");}
    {Model model;model.kind=2;Problem p(model);p.derivative[0]=-.5;const auto s=module.solve(p.request,{},[](std::size_t){});const double y=s.values.back();require(std::abs(std::log(1/y)+.5*(1-y*y)-1)<2e-8,"state-dependent implicit mass matches separable integral");}
    {Model model;model.kind=4;Problem p(model);p.initial[0]=0;p.derivative[0]=1;p.algebraic[0]=1;const auto s=module.solve(p.request,{},[](std::size_t){});require(std::abs(s.values.back()-std::sin(1))<2e-8,"pure time-dependent algebraic constraint");}
    {Model model;Problem p(model,5);for(unsigned j=0;j<5;++j){p.initial[j]=1+j;p.derivative[j]=-2*p.initial[j];}const auto s=module.solve(p.request,{},[](std::size_t){});for(unsigned j=0;j<5;++j)require(std::abs(s.values[(s.report.samples-1)*5+j]-(1+j)*std::exp(-2))<1e-7,"all five coefficient coordinates retained");}
    {Model model;model.kind=3;Problem p(model,2);p.initial[0]=0;p.initial[1]=1;p.derivative[0]=1;p.derivative[1]=0;p.algebraic[1]=1;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"higher-index constrained formulation refused");}
    {Model model;model.kind=5;Problem p(model,2);p.initial[0]=1;p.initial[1]=0;p.derivative[0]=0;p.derivative[1]=-1;p.request.event_context=&model;p.request.event=Model::event;p.request.event_direction=-1;p.request.reset_context=&model;p.request.reset=Model::reset;p.request.t1=2.2;const auto s=module.solve(p.request,{},[](std::size_t){});const double impact=std::sqrt(2.0),elapsed=2.2-impact,expected=.8*std::sqrt(2.0)*elapsed-.5*elapsed*elapsed;
        require(s.report.events==1&&std::abs(s.eventTimes[0]-impact)<1e-7,"IDAS continuous event localization");require(std::abs(s.values[s.values.size()-2]-expected)<2e-7,"native bounce reset/restart analytic trajectory");require(std::abs(s.eventAfter[1]+.8*s.eventBefore[1])<1e-10,"native ordered before/after event records");
        p.request.reset=nullptr;p.request.event_terminal=1;const auto terminal=module.solve(p.request,{},[](std::size_t){});require(terminal.report.terminal_event&&std::abs(terminal.times.back()-impact)<1e-7,"terminal event returns localized time");
        p.request.reset=Model::reset;p.request.event_terminal=0;p.request.t1=5;p.request.max_events=1;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"hybrid event count budget");p.request.max_events=100;p.request.minimum_event_interval=4;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"minimum event interval/chattering policy");}
    {Model model;Problem p(model);p.request.max_evaluations=1;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"DAE residual budget");p.request.max_evaluations=100000;p.request.max_steps=1;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"DAE step budget");p.request.max_steps=100000;rejects([&]{module.solve(p.request,{},[](std::size_t){throw std::runtime_error("quota");});},"DAE owner charge exception");auto flag=std::make_shared<std::atomic_bool>(false);model.calls=0;model.cancellation=flag;rejects([&]{module.solve(p.request,flag,[](std::size_t){});},"DAE cooperative cancellation");model.cancellation.reset();require(std::abs(module.solve(p.request,{},[](std::size_t){}).values.back()-std::exp(-2))<2e-8,"DAE reusable after cancellation");}
}
#ifndef PENTA_DAE_KERNEL_ONLY
void languageTests(){
    penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-dae-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";
    auto run=[&](const std::string& source,bool good=true){const auto result=penta::runtime::execute(source,{},options);require(result.ok()==good,"compiled DAE language case");if(result.ok()!=good){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}};
    const std::string decay="native function real residual(real t,real y,real yp,real rate){return yp+rate*y;}";
    run(decay+"record a=daesolve(residual,1.0,-2.0,options(parameters=options(rate=2.0),algebraic=[0],t1=.2));assert(abs(a.values(end,1)-exp(-.4))<1e-7);record b=daesolve(residual,1.0,-3.0,options(parameters=options(rate=3.0),algebraic=[0],t1=.2));assert(a.cache_key==b.cache_key&&b.cache_hit);assert(a.method==\"idas_bdf\"&&a.jacobian==\"analytic\");");
    run("native function [real first,real second] residual(real t,real y,real z,real dy,real dz){return dy+y-z,z-sin(t);}record a=daesolve(residual,[0,.3],[.7,1],options(algebraic=[0,1],initialization=\"consistent\",t1=.2));assert(abs(a.consistent_initial(2))<1e-8&&abs(a.consistent_derivative(1))<1e-8);assert(abs(a.values(end,2)-sin(.2))<1e-7);");
    run("native function pentarnion residual(real t,pentarnion p,pentarnion dp,real rate){return dp+rate*p;}pentarnion initial=1+2*i+3*j+4*k+5*ell;record a=daesolve(residual,initial,-2*initial,options(parameters=options(rate=2.0),algebraic=zeros(1,5),t1=.2));assert(norm(a.values(end,:)-exp(-.4)*[1,2,3,4,5],2)<1e-6);");
    run("native function [real first,real second] residual(real t,real h,real v,real dh,real dv){return dh-v,dv+1.0;}native function real ground(real t,real h,real v,real dh,real dv){return h;}native function [real h,real v,real dh,real dv] bounce(real t,real height,real velocity,real dheight,real dvelocity,real e){return 0.0,-e*velocity,-e*velocity,-1.0;}record a=daesolve(residual,[1,0],[0,-1],options(algebraic=[0,0],event=ground,event_direction=-1,reset=bounce,reset_parameters=options(e=.8),t1=2.2));assert(a.events==1&&abs(a.event_times(1)-sqrt(2))<1e-6);assert(a.reset_cache_key!=\"\");");
    run(decay+"record a=daesolve(residual,1.0,0.0,options(parameters=options(rate=2.0),algebraic=[0]));",false);
    run(decay+"record a=daesolve(residual,1.0,-2.0,options(parameters=options(rate=2.0)));",false);
    run(decay+"record a=daesolve(residual,1.0,-2.0,options(parameters=options(rate=2.0),algebraic=[0],max_native_operations=1));",false);
    run("native function [real first,real second] residual(real t,real y,real z,real dy,real dz){return dy-z,y;}record a=daesolve(residual,[0,1],[1,0],options(algebraic=[0,1]));",false);
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){try{nativeTests();
#ifndef PENTA_DAE_KERNEL_ONLY
languageTests();
#endif
}catch(const std::exception& failure){++failures;std::cerr<<"UNEXPECTED: "<<failure.what()<<'\n';}std::cout<<checks<<" DAE checks, "<<failures<<" failures\n";return failures?1:0;}
