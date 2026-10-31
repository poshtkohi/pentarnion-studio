#include "inference/uncertainty.hpp"
#include "sensitivity/solver_summary.hpp"
#include "runtime/interpreter.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
using namespace penta;
namespace {int checks=0;void check(bool condition,const char* text){++checks;if(!condition)throw std::runtime_error(text);}void run(const std::string& source,const runtime::ExecutionOptions& options){++checks;const auto result=runtime::execute(source,{},options);if(result.ok())return;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<" near "<<source.substr(d.span.offset,100)<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<" near "<<source.substr(d.span.offset,100)<<'\n';throw std::runtime_error("Native uncertainty language fixture failed.");}}
int main(int argc,char** argv){
    runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-inference-uncertainty-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/"kernels";if(argc>1)options.numericModuleDirectory=std::filesystem::absolute(argv[1]);
    try{
        using inference::noise::Model;for(auto model:{Model::Lognormal,Model::StudentT,Model::Poisson})for(double observed:{1.,3.,8.})for(double predicted:{.7,2.,8.}){const double h=1e-5;const auto estimate=inference::noise::residual(model,predicted,observed,.4,4);const double fd=(inference::noise::residual(model,predicted+h,observed,.4,4).value-inference::noise::residual(model,predicted-h,observed,.4,4).value)/(2*h);check(std::abs(fd-estimate.derivative)<1e-5,"Native likelihood residual Jacobian agrees with independent centered differences");}
        const std::array<double,1> point{2};auto wald=inference::waldInterval(point,std::array<double,4>{1,1,1,1},4,.95);check(wald.valid&&std::abs(wald.upper[0]-2-1.959963984540054/2)<1e-10,"Known-variance Wald interval matches the analytic constant-mean model");const auto regions=inference::profileRegions(std::array<double,5>{-2,-1,0,1,2},std::array<double,5>{4,1,0,1,4},std::array<double,5>{1,1,1,1,1},1);check(regions.size()==1&&regions[0].lower==-1&&regions[0].upper==1&&!regions[0].lowerOpen&&!regions[0].upperOpen,"Sampled likelihood profile brackets both threshold crossings");
        const std::array<double,8> samples{1,2,2,4,3,6,4,8};const auto bands=inference::predictionBands(samples,4,2,.95);check(bands.mean[0]==2.5&&bands.mean[1]==5,"Prediction ensemble moments retain correlated outputs");check(bands.simultaneousLower[0]<bands.mean[0]&&bands.simultaneousUpper[1]>bands.mean[1],"Gridwise simultaneous bands use complete joint prediction rows");const auto a=inference::observationSamples(samples,4,2,2048,Model::Gaussian,.2,4,42),b=inference::observationSamples(samples,4,2,2048,Model::Gaussian,.2,4,42);check(a==b,"Future observation draws are repeatable from immutable row streams");
        run(R"(
native function real constant(real a,real x){return a;}
record logfit=multistartfit(constant,[1;3],[1,2,3],[1,2,4],options(objective="lognormal_known_variance",noise_variance=0.04,lower=0.01));assert(abs(logfit.best_parameters(1)-2)<1e-6);
record poisson=multistartfit(constant,[1;4],[1,2,3],[1,2,6],options(objective="poisson_deviance",lower=0.01));assert(abs(poisson.best_parameters(1)-3)<1e-5);
record robust=multistartfit(constant,[0;1],[1,2,3,4],[0,0,0,100],options(objective="student_t_known_scale",noise_variance=1,noise_degrees=4));assert(abs(robust.best_parameters(1))<0.1);
record boot=fitbootstrap(constant,2,[1,2,3,4],[1,2,3,2],options(samples=16,sample_method="parametric",objective="gaussian_known_variance",noise_variance=0.04,threads=2));assert(boot.successful==16);record bands=predictionbands(boot,options(noise="gaussian",noise_variance=0.04,draws=4096,seed=7));assert(bands.future_available);assert(norm(bands.future_upper-bands.future_lower)>norm(bands.fitted_upper-bands.fitted_lower));
record profile=profilefit(constant,2,[1,2,3],[1,2,3],1,[1,1.5,2,2.5,3],options(objective="gaussian_known_variance",noise_variance=0.04));record intervals=profileinterval(profile);assert(intervals.regions==1);assert(intervals.lower_open(1)==0);assert(intervals.upper_open(1)==0);
native function real square(real a,real x){return a*a;}
record fits=multistartfit(square,[-2;2],[1,2,3],[4,4,4]);record ambiguity=identifiability(fits);assert(ambiguity.clusters==2);assert(ambiguity.practical_ambiguity);
native function real line(real a,real b,real x){return a*x+b;}
record study=coveragefit(constant,[2],1:8,options(initial=[1],samples=96,threads=4,objective="gaussian_known_variance",noise_variance=0.04,seed=42));assert(study.coverage.valid_intervals==96);assert(study.coverage.conditional_coverage(1)>0.8);assert(abs(study.coverage.bias(1))<0.1);
record propagated=propagateprediction(line,[1,0;1.1,0;0.9,0],[0,1,2],options(threads=2));record spread=predictionbands(propagated);assert(abs(spread.fitted_mean(3)-2)<1e-12);assert(spread.future_available==0);
record structural=structuralidentifiability(line);assert(structural.supported);assert(structural.globally_identifiable);assert(structural.rank==2);
native function real confounded(real a,real b,real x){return (a+b)*x;}
record certificate=structuralidentifiability(confounded);assert(certificate.supported);assert(certificate.rank==1);assert(certificate.globally_identifiable==0);assert(size(certificate.nullspace,1)==1);
record nonlinear=structuralidentifiability(square);assert(nonlinear.supported==0);
)",options);
        run(R"(
ode decay(t,real x;real rate){x'=-rate*x;}
vector t=[0,0.25,0.5,1];matrix y=(2*exp(-0.7*t))';record fit=odemultistartfit(decay,[0.2;1.2],t,y,options(initial=2.0,parameters=options(rate=0.3),objective="lognormal_known_variance",noise_variance=0.01));assert(fit.successful==2);assert(norm(fit.parameters-[0.7;0.7])<1e-6);
)",options);
        const auto oscillator=language::analyse("ode oscillator(t,real x,real y;real omega){x'=-omega*y;y'=omega*x;}");check(oscillator.ok(),"Dynamical summary fixture parses");sensitivity::solver::Model model;model.kind=sensitivity::solver::Kind::Ode;model.native.cacheDirectory=options.odeCacheDirectory;model.ode=std::make_shared<ode::PreparedModel>(*oscillator.program.statements.front().ode,model.native);model.initial={1,0};model.parameters={2};model.parameterCoordinates={0};model.stateCoordinates=2;model.odeOptions.t1=20;model.odeOptions.sampleStep=.01;model.odeOptions.rtol=1e-10;model.odeOptions.atol=1e-12;model.moduleDirectory=options.numericModuleDirectory;model.summary=sensitivity::solver::Summary::Period;model.validate(1);const std::array<double,1> omega{2};check(std::abs(model.evaluate(omega)-std::acos(-1.0))<1e-5,"Sampled native period matches the independent analytic oscillator");model.summary=sensitivity::solver::Summary::LargestLyapunov;check(std::abs(model.evaluate(omega))<1e-6,"Native largest-exponent summary matches the zero exponent of a rigid rotation");
        run(R"(
ode decay(t,real x;real rate){x'=-rate*x;}
native function real threshold(real t,real x,real rate){return x-0.5;}
record event_sensitivity=modelsensitivity(decay,[0.5],[1.5],options(initial=1.0,parameters=options(rate=1.0),summary="event_time",method="morris",samples=4,levels=4,threads=2,solver=options(t1=5,sample_step=0.05,mass=[2],event=threshold,event_direction=-1,event_terminal=1)));
assert(event_sensitivity.mu_star(1)>0);
)",options);
        run(R"(
native function [real r1,real r2] residual(real t,real x,real z,real xp,real zp,real rate){return xp+rate*x,z-x*x;}
record sensitivity_result=modelsensitivity(residual,[0.5],[1.5],options(kind="dae",initial=[1,1],initial_derivative=[-1,-2],algebraic=[0,1],parameters=options(rate=1.0),summary="final",coefficient=2,method="morris",samples=4,levels=4,threads=2,solver=options(t1=1,sample_step=0.1,initialization="consistent")));
assert(sensitivity_result.mu_star(1)>0);
)",options);
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" native likelihood/uncertainty checks passed.\n";
}
