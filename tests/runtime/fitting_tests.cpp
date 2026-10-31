#include "fitting/fit_module.hpp"
#ifndef PENTA_FITTING_KERNEL_ONLY
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
bool close(double a,double b,double tolerance=1e-7){return std::abs(a-b)<=tolerance*(1+std::abs(b));}
struct Context{int kind=0;std::shared_ptr<std::atomic_bool> cancellation;};
int evaluate(void* opaque,const double* x,uint64_t n,double* f,uint64_t m,double* j,char*,uint64_t){
    const auto& c=*static_cast<Context*>(opaque);
    if(c.kind==1){for(uint64_t r=0;r<m;++r){f[r]=x[0]-(r+1==m?100:1);if(j)j[r]=1;}}
    else if(c.kind==2){for(uint64_t r=0;r<m;++r){f[r]=x[0]+x[1]-1;if(j){j[r*n]=1;j[r*n+1]=1;}}}
    else if(c.kind==3){for(uint64_t r=0;r<m;++r)f[r]=std::numeric_limits<double>::quiet_NaN();}
    else if(c.kind==4||c.kind==5){const double factor=c.kind==4?1e200:1e-200;f[0]=factor*(x[0]-1);if(j)j[0]=factor;}
    else{for(uint64_t r=0;r<m;++r){f[r]=x[0]+static_cast<double>(r)*x[1]-(1+2*static_cast<double>(r));if(j){j[r*n]=1;j[r*n+1]=static_cast<double>(r);}}}
    if(c.cancellation)c.cancellation->store(true);
    return 0;
}
penta_fit_request_v1 request(Context& context,const std::vector<double>& initial,uint64_t m=3){penta_fit_request_v1 q{};q.struct_size=sizeof(q);q.method=1;q.dimension=initial.size();q.residual_count=m;q.initial=initial.data();q.max_iterations=100;q.max_evaluations=10000;q.history_capacity=101;q.threads=2;q.loss_scale=1;q.function_tolerance=q.gradient_tolerance=q.parameter_tolerance=1e-12;q.maximum_wall_seconds=30;q.noise_variance=-1;q.callback_context=&context;q.evaluate=evaluate;return q;}
template<class F>void rejects(F task,int status,const std::string& label){try{task();expect(false,label);}catch(const penta::fitting::Error& error){expect(error.status==status,label+": "+error.what());}}
void kernels(){
    using namespace penta::fitting;Module module;expect(!module.loaded(),"fit module starts unloaded");Context context;std::vector<double> initial{0,0};auto q=request(context,initial);q.covariance=1;q.noise_variance=1;
    const auto fit=module.solve(q);expect(module.loaded(),"fit lazily loads Ceres module");expect(fit.report.converged&&close(fit.solution[0],1)&&close(fit.solution[1],2),"LM independent linear optimum");expect(fit.report.cost<1e-16&&fit.report.rank==2,"residual and rank agree independently");expect(fit.report.covariance_valid&&close(fit.covariance[0],5./6)&&close(fit.covariance[1],-.5)&&close(fit.covariance[3],.5),"known-noise covariance matches analytic inverse information matrix");expect(fit.report.jacobian_evaluations>0&&fit.report.history_rows>0,"fit history and native Jacobian statistics");expect(fit.report.threads==2&&fit.report.threads_used>=1&&fit.report.threads_used<=2,"configured Ceres limit and observed worker participation are distinct");
    q.method=2;const auto dogleg=module.solve(q);expect(dogleg.report.converged&&close(dogleg.solution[0],1)&&close(dogleg.solution[1],2),"Dogleg independent optimum");
    std::vector<double> scale{10,.1};q.parameter_scale=scale.data();const auto scaled=module.solve(q);expect(close(scaled.solution[0],1)&&close(scaled.solution[1],2),"physical parameter scaling retains optimum");q.parameter_scale=nullptr;
    std::vector<double> lower{-10,-10},upper{.5,10};q.lower=lower.data();q.upper=upper.data();q.method=1;const auto bounded=module.solve(q);if(!close(bounded.solution[1],2.3))std::cerr<<"Bounded diagnostic: "<<bounded.solution[0]<<','<<bounded.solution[1]<<" cost="<<bounded.report.cost<<" status="<<bounded.report.status<<" message="<<bounded.report.message<<'\n';expect(close(bounded.solution[0],.5)&&close(bounded.solution[1],2.3),"bounded least squares optimum");expect(!bounded.report.covariance_valid,"active-bound covariance unavailable");q.lower=q.upper=nullptr;
    std::vector<double> weights{1,4,1};q.weights=weights.data();const auto weighted=module.solve(q);expect(close(weighted.covariance[0],2./3)&&close(weighted.covariance[1],-.5),"weighted known-noise covariance");q.weights=nullptr;
    std::vector<double> starts{4,-3,-2,8};q.start_count=2;q.starts=starts.data();const auto multistart=module.solve(q);expect(multistart.report.starts_completed==2&&close(multistart.solution[0],1)&&close(multistart.solution[1],2),"all explicit starts share native residual and operation budget");q.start_count=0;q.starts=nullptr;
    context.kind=2;const auto deficient=module.solve(q);expect(deficient.report.rank==1&&!deficient.report.covariance_valid,"rank deficiency does not fabricate covariance");
    std::vector<double> exactInitial{1};context.kind=4;q=request(context,exactInitial,1);q.covariance=1;q.noise_variance=1e300;const auto hugeJacobian=module.solve(q);expect(hugeJacobian.report.covariance_valid&&std::abs(hugeJacobian.covariance[0]/1e-100-1)<1e-12,"known covariance avoids overflowing singular value squared");context.kind=5;q.noise_variance=1e-300;const auto tinyJacobian=module.solve(q);expect(tinyJacobian.report.covariance_valid&&std::abs(tinyJacobian.covariance[0]/1e100-1)<1e-12,"known covariance avoids underflowing singular value squared");
    context.kind=1;std::vector<double> scalar{0};q=request(context,scalar,5);q.loss=1;const auto huber=module.solve(q);expect(huber.report.converged&&close(huber.solution[0],1.25,2e-6),"Huber loss is per residual coordinate");q.loss=0;const auto linear=module.solve(q);expect(close(linear.solution[0],20.8,1e-6),"linear outlier objective separately verified");
    context.kind=0;q=request(context,initial);q.max_evaluations=1;const auto limited=module.solve(q);expect(!limited.report.converged&&limited.report.evaluations==1&&std::isfinite(limited.report.cost),"strict exhausted evaluation budget returns finite nonconvergence");
    context.kind=3;q=request(context,initial);rejects([&]{(void)module.solve(q);},PENTA_FIT_DOMAIN,"nonfinite residual rejected");context.kind=0;
    q=request(context,initial);auto cancellation=std::make_shared<std::atomic_bool>(true);rejects([&]{(void)module.solve(q,cancellation);},PENTA_FIT_CANCELLED,"pre-cancelled fit rejected");cancellation->store(false);context.cancellation=cancellation;rejects([&]{(void)module.solve(q,cancellation);},PENTA_FIT_CANCELLED,"native callback cancellation");context.cancellation.reset();
    weights={0,0,0};q.weights=weights.data();rejects([&]{(void)module.solve(q);},PENTA_FIT_ARGUMENT,"all-zero weights rejected");q.weights=nullptr;q.noise_variance=-.5;rejects([&]{(void)module.solve(q);},PENTA_FIT_ARGUMENT,"invalid negative supplied variance rejected");
    const auto missing=std::filesystem::temp_directory_path()/("penta-fit-missing-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(missing);q=request(context,initial);rejects([&]{(void)Module(missing).solve(q);},PENTA_FIT_INTERNAL,"missing optional fitting module diagnosed");std::filesystem::remove(missing);
}
#ifndef PENTA_FITTING_KERNEL_ONLY
void runtime(){
    using namespace penta::runtime;ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-fit-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache"/"odes";
    auto run=[&](const std::string& source,bool good=true){const auto result=execute(source,{},options);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<" at "<<source.substr(d.span.offset,80)<<'\n';}expect(result.ok()==good,"fitting language: "+source.substr(0,70));return result;};
    run("native function [real a,real b,real c] residual(real x,real y){return x-1.0,x+y-3.0,x+2.0*y-5.0;}record r=lsqfit(residual,[0,0],options(covariance=1,noise_variance=1.0));assert(r.converged);assert(norm(r.x-[1,2])<1e-7);assert(r.rank==2);assert(r.covariance_valid);record s=lsqnonlin(residual,[2,4],options(method=\"dogleg\",jacobian=\"finite_difference\"));assert(s.converged);assert(s.cache_hit);assert(norm(s.x-[1,2])<1e-7);");
    run("native function pentarnion R(pentarnion p,pentarnion a,pentarnion b){return a*p-b;}pentarnion expected=1+i-j+0.25*ell;pentarnion a=2+0.2*j;pentarnion b=a*expected;record r=lsqfit(R,0.0,options(parameters=options(a=a,b=b)));assert(r.converged);assert(abs(coeff(r.x-expected,0))<1e-7);assert(abs(coeff(r.x-expected,1))<1e-7);assert(abs(coeff(r.x-expected,2))<1e-7);assert(abs(coeff(r.x-expected,4))<1e-7);");
    run("native function real R(real x,real target){return x*x-target;}threads(3);record r=lsqfit(R,0.2,options(parameters=options(target=4.0),lower=0.0,upper=3.0,multistart=3,seed=42,threads=1));assert(r.converged);assert(abs(r.x-2)<1e-7);assert(r.starts_completed==3);assert(threads()==3);");
    run("native function real R(real x){return x*x-2.0;}record r=lsqfit(R,1.0,options(typo=1));",false);
    run("native function real R(real x){return x*x-2.0;}record r=lsqfit(R,1.0,options(weights=-1.0));",false);
    run("native function real R(real x){return x*x-2.0;}record r=lsqfit(R,1.0,options(noise_variance=-0.5));",false);
    run("native function real R(real x){return x*x-2.0;}record r=lsqfit(R,1.0,options(max_evaluations=1));assert(!r.converged);assert(r.evaluations==1);");
    run("native function real R(real x){if(x<0.0){return sqrt(x);}return x-1.0;}threads(0);record r=lsqfit(R,0.0,options(lower=0.0,upper=2.0,jacobian=\"finite_difference\",history_limit=0));assert(r.converged);assert(abs(r.x-1.0)<1e-7);assert(r.backend_threads==1);assert(r.threads_used==1);assert(numel(r.history)==0);");
    run("native function real R(real x){return x-1.0;}record r=lsqfit(R,0.0,options(history_limit=100000));",false);
    run("native function real line(real a,real b,real x){return a+b*x;}record r=curvefit(line,[0,0],0:4,[1.1,2.8,5.1,7.2,8.8],options(covariance=1));assert(r.converged);assert(norm(r.x-[1.04,1.98])<1e-7);assert(r.rank==2);assert(r.covariance_valid);assert(norm(r.fitted-[1.04;3.02;5.0;6.98;8.96])<1e-7);record s=curvefit(line,[0,0],0:4,[2,5,8,11,14],options(jacobian=\"finite_difference\"));assert(s.cache_hit);assert(norm(s.x-[2,3])<1e-6);");
    run("native function pentarnion model(pentarnion p,real t){return (1.0+t)*p;}pentarnion target=1+i-j+0.25*ell;matrix data=[target;2*target;3*target];record r=curvefit(model,0.0,[0,1,2],data,options(coefficient_weights=[1,2,2,2,2]));assert(r.converged);assert(abs(coeff(r.x-target,0))<1e-7);assert(abs(coeff(r.x-target,1))<1e-7);assert(abs(coeff(r.x-target,4))<1e-7);");
    run("ode decay(t,real y;real rate){y'=-rate*y;}vector t=[0.2,0.7,1.3,2.0];matrix observed=(2.0*exp(-0.7*t))';record r=odefit(decay,1.0,t,observed,options(parameters=options(rate=0.2),fit_initial=1,t0=0.0,rtol=1e-10,atol=1e-12));assert(r.converged);assert(abs(r.parameters.rate-0.7)<1e-6);assert(abs(r.initial-2.0)<1e-6);assert(r.sensitivity_method==\"cvodes_forward\");assert(r.jacobian_available);matrix exact=-t'.*r.fitted;assert(norm(r.jacobian(:,1)-exact)<1e-6);record s=odefit(decay,2.0,t,observed,options(parameters=options(rate=0.3),t0=0.0,jacobian=\"finite_difference\",rtol=1e-10,atol=1e-12));assert(s.cache_hit);assert(s.ode_cache_hit);assert(abs(s.parameters.rate-0.7)<1e-5);");
    run("ode decay(t,real y){y'=-y;}vector t=[0,0.2,0.7,1.3];matrix observed=(2.0*exp(-t))';record r=odefit(decay,1.0,t,observed,options(fit_parameters=\"none\",fit_initial=1));assert(r.converged);assert(abs(r.initial-2.0)<1e-6);");
    run("ode decay(t,real y;real rate){y'=-rate*y;}vector t=[0,0.2,0.7,1.3];matrix observed=(2.0*exp(-0.35*t))';record r=odefit(decay,2.0,t,observed,options(parameters=options(rate=0.2),mass=[2.0],rtol=1e-10,atol=1e-12));assert(r.converged);assert(abs(r.parameters.rate-0.7)<1e-6);");
    run("ode decay(t,real y;real rate){y'=-rate*y;}record r=odefit(decay,1.0,[0,1],[1.0;0.5],options(parameters=options(rate=0.2),fit_parameters=\"typo\"));",false);
    auto small=options;small.maxMatrixElements=4;expect(!execute("native function [real a,real b,real c] R(real x,real y){return x,y,x+y;}record r=lsqfit(R,[0,0]);",{},small).ok(),"returned Jacobian budget rejected before solve");
#ifdef PENTA_SOURCE_DIR
    for(const auto* name:{"main.p","ode_fit.p","pentarnion_fit.p"}){std::ifstream stream(std::filesystem::path(PENTA_SOURCE_DIR)/"examples/least-squares"/name,std::ios::binary);const std::string source((std::istreambuf_iterator<char>(stream)),{});expect(!source.empty(),std::string("least-squares example exists: ")+name);if(!source.empty())run(source);}
#endif
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){kernels();
#ifndef PENTA_FITTING_KERNEL_ONLY
runtime();
#endif
std::cout<<checks<<" fitting checks, "<<failures<<" failures\n";return failures?1:0;}
