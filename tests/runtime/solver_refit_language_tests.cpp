#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>

using namespace penta;
namespace {
int checks=0;
void run(const std::string& source,const runtime::ExecutionOptions& options,bool valid=true){++checks;const auto result=runtime::execute(source,{},options);if(result.ok()==valid)return;for(const auto& diagnostic:result.analysis.diagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<'\n';for(const auto& diagnostic:result.runtimeDiagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<" near "<<source.substr(diagnostic.span.offset,100)<<'\n';throw std::runtime_error("Unexpected solver-refit language fixture "+std::to_string(checks));}
}
int main(int argc,char** argv){
    runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-solver-refit-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";if(argc>1)options.numericModuleDirectory=std::filesystem::absolute(argv[1]);
    try{
        run(R"(
ode decay(t,real y;real rate){y'=-rate*y;}
vector t=[0,0.25,0.5,1];matrix observed=(2*exp(-0.7*t))';
record fit=odemultistartfit(decay,[0.2;0.5;1.1;1.6],t,observed,options(initial=2.0,parameters=options(rate=0.3),threads=4,checkpoint="ode-refits.json",solver=options(rtol=1e-10,atol=1e-12)));
assert(fit.successful==4);assert(norm(fit.parameters-0.7*ones(4,1))<1e-6);assert(fit.jacobian=="compiler_cvodes_forward");assert(fit.inner_threads==1);
record resumed=odemultistartfit(decay,[0.2;0.5;1.1;1.6],t,observed,options(initial=2.0,parameters=options(rate=0.3),threads=1,resume=1,checkpoint="ode-refits.json",solver=options(rtol=1e-10,atol=1e-12)));
assert(resumed.restored==4);assert(resumed.native_solver_evaluations==0);assert(resumed.parameters==fit.parameters);
record profile=odeprofilefit(decay,0.3,t,observed,1,[0.6,0.7,0.8],options(initial=2.0,parameters=options(rate=0.3),threads=2));
assert(profile.successful==3);assert(profile.costs(2)<1e-12);assert(profile.delta_sse(1)>0);assert(profile.delta_sse(3)>0);
record boot=odefitbootstrap(decay,0.3,t,observed,options(initial=2.0,parameters=options(rate=0.3),samples=8,sample_method="residual",threads=2,seed=42));
assert(boot.successful==8);assert(boot.statistics_valid);assert(size(boot.fitted_curves,2)==4);assert(boot.band_kind=="pointwise_fitted_mean_percentile");
)",options);
        run(R"(
native function real reaction(real u,real t,real x,real rate){return -rate*u;}
vector t=[0,0.05,0.1,0.2];matrix observed=(2*exp(-0.7*t))';matrix initial=2*ones(1,5);vector x=linspace(0,1,5);
record fit=pdemultistartfit(reaction,[0.2;0.5;1.1],t,observed,options(initial=initial,x=x,parameters=options(rate=0.3),fit_parameters="rate",observation_coordinates=[3],threads=3,solver=options(boundary="neumann",diffusion=0.1,time_step=0.005,sample_step=0.05)));
assert(fit.successful==3);assert(norm(fit.parameters-0.7*ones(3,1))<1e-5);assert(fit.jacobian=="finite_difference_native_solves");assert(fit.inner_threads==1);
record profile=pdeprofilefit(reaction,0.3,t,observed,1,[0.6,0.7,0.8],options(initial=initial,x=x,parameters=options(rate=0.3),fit_parameters="rate",observation_coordinates=[3],threads=2,solver=options(boundary="neumann",diffusion=0.1,time_step=0.005,sample_step=0.05)));
assert(profile.successful==3);assert(profile.costs(2)<1e-10);
record boot=pdefitbootstrap(reaction,0.3,t,observed,options(initial=initial,x=x,parameters=options(rate=0.3),fit_parameters="rate",observation_coordinates=[3],samples=8,threads=2,solver=options(boundary="neumann",diffusion=0.1,time_step=0.005,sample_step=0.05)));
assert(boot.successful==8);assert(boot.statistics_valid);assert(size(boot.fitted_curves,2)==4);
)",options);
        run(R"(
algebra H=builtin_algebra("quaternion");
ode decay(t,element<H> q;real rate){q'=-rate*q;}
vector t=[0,0.25,0.5,1];matrix observed=(2*exp(-0.7*t))';
record fit=odemultistartfit(decay,[0.2;1.1],t,observed,options(initial=2*H.one,parameters=options(rate=0.3),observation_coordinates=[1],threads=2));
assert(fit.successful==2);assert(norm(fit.parameters-[0.7;0.7])<1e-5);
)",options);
        run(R"(
native function real reaction(real u,real t,real x,real rate){return -rate*u;}
vector t=[0,0.07,0.1,0.2];matrix observed=(2*exp(-0.7*t))';
record fit=pdemultistartfit(reaction,[0.2;0.5],t,observed,options(initial=2*ones(1,5),x=linspace(0,1,5),parameters=options(rate=0.3),fit_parameters="rate",observation_coordinates=[3],solver=options(boundary="neumann",time_step=0.005,sample_step=0.05)));
assert(fit.successful==0);assert(fit.task_status==[3;3]);
)",options);
        run("ode d(t,real x;real rate){x'=-rate*x;}record f=odemultistartfit(d,[0.2;0.5],[0,1],[1;0.5],options(parameters=options(rate=0.3)));",options,false);
#ifdef PENTA_SOURCE_DIR
        for(const auto* path:{"parallel-solver-refits/ode-multistart-profile-and-bootstrap.p","parallel-solver-refits/pde-parameter-refits-at-an-observed-node.p","parallel-map-ensembles/checkpointed-independent-noise-streams.p","tiered-ode-verification/checkpointed-verification-tiers.p"}){std::ifstream file(std::filesystem::path(PENTA_SOURCE_DIR)/"examples"/path,std::ios::binary);const std::string source((std::istreambuf_iterator<char>(file)),{});if(source.empty())throw std::runtime_error("Missing extension example source.");run(source,options);}
#endif
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" native solver-refit language fixtures passed.\n";
}
