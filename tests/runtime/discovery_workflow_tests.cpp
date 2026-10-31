#include "discovery/basin_candidates.hpp"
#include "discovery/failure_reason.hpp"
#include "runtime/interpreter.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
using namespace penta;
namespace {int checks=0;void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}void run(const std::string& source,const runtime::ExecutionOptions& options,bool valid=true){++checks;const auto result=runtime::execute(source,{},options);if(result.ok()==valid)return;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<" near "<<source.substr(d.span.offset,100)<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<" near "<<source.substr(d.span.offset,100)<<'\n';throw std::runtime_error("Discovery language fixture failed.");}}
int main(int argc,char** argv){
    runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-discovery-workflow-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";options.maxNativeOperations=2000000000;if(argc>1)options.numericModuleDirectory=std::filesystem::absolute(argv[1]);
    try{
        using discovery::FailureReason;check(discovery::classifyFailure("O1003","time")==FailureReason::WallTime,"Native timeout has a distinct reason");check(discovery::classifyFailure("","max_steps exhausted")==FailureReason::StepLimit,"Step-limit is only possible stiffness, never a proof");check(discovery::classifyFailure("","division by zero")==FailureReason::SingularRhs,"Singular RHS is distinguished");check(discovery::classifyFailure("","nonfinite output")==FailureReason::NonfiniteDomain,"Nonfinite output is distinguished");check(discovery::classifyFailure("","linear solver setup failed")==FailureReason::LinearSolver,"Linear solver diagnostics survive");check(discovery::classifyFailure("","native work budget reached")==FailureReason::NativeWorkLimit,"Work budget differs from a numerical failure");
        discovery::basin::Tail a{{-1,-1.01,-.99},1,true},b{{-.99,-1,-1.01},1,true},c{{1,1.01,.99},1,true};check(discovery::basin::cloudDistance(a,b)==0,"Tail comparison is independent of phase/sample order");discovery::basin::Catalogue catalogue;check(catalogue.classify(a)==1&&catalogue.classify(b)==1&&catalogue.classify(c)==2,"Equivalent clouds merge and separated clouds receive distinct stable IDs");discovery::basin::Tail ambiguous{{-.8},1,true};check(catalogue.classify(ambiguous)==0,"Grey-zone clouds remain unresolved");
        run(R"(
ode bistable(t,real x){x'=x-x*x*x;}
record b=basins(bistable,[-2;-0.5;0.2;1;2;4],options(t1=16,sample_step=0.05,escape_radius=3,threads=3,cluster_tolerance=0.01));
assert(b.labels(1)==b.labels(2));assert(b.labels(3)==b.labels(4));assert(b.labels(4)==b.labels(5));assert(b.labels(1)!=b.labels(3));assert(b.labels(6)==-1);assert(b.attractors==2);assert(b.resolved_count==5);assert(norm(b.fractions-[2/6;3/6])<1e-12);assert(norm(b.conditional_fractions-[2/5;3/5])<1e-12);assert(b.escaped==1);
)",options);
        run(R"(
ode decay(t,real x;real rate){x'=-rate*x;}
record scan=lyapunovscan(decay,[1],options(parameters=options(rate=searchset([0.5,1])),t1=0.5,sample_step=0.05,horizons=[1,2],minimum_observation=0.2,qr_step=0.25,threads=2));
assert(scan.verification.rows==4);assert(scan.catalogue_runs==2);assert(abs(scan.verification.exponents(3,1)+0.5)<1e-6);assert(abs(scan.verification.exponents(4,1)+1)<1e-6);assert(scan.verification.tier_converged(3));assert(scan.verification.tier_converged(4));
)",options);
        run(R"(
ode torus(t,real x,real y,real u,real v){x'=-y;y'=x;u'=-sqrt(2)*v;v'=sqrt(2)*u;}
record s=lyapunovscan(torus,[1,0,1,0],options(t1=20,sample_step=0.1,horizons=[40,80],qr_step=1,return_tolerance=0.4,min_return_time=1,minimum_observation=20,lyapunov_tolerance=0.01));
assert(s.verification.near_zero_exponents(2)==4);assert(s.verification.quasiperiodic_candidate(2));assert(s.verification.status_codes(2)==11);assert(s.verification.positive(2)==0);
)",options);
        run(R"(
ode singular(t,real x){x'=1/(x-1);}
record s=discover(singular,[1],options(t1=0.1));record status=discoverstatus(s,1);assert(status.failure_reason_code==8);assert(s.failure_reason_codes(1)==8);assert(status.status=="numerical_failure");
)",options);
        run(R"(
ode model(t,real x;real a,real b){x'=-a*x+b;}
native function real force(real t,real x,real a,real b){return -a*x+b;}
record e=discover(model,[1],options(parameters=options(a=searchset([1,2]),b=searchset([0.1,0.2])),t1=0.2,sample_step=0.05));
record out=exportexperiment(model,e,options(directory="plots",ids=[1,2,3,4],kinds="heatmap,force,return_error",derived=options(force=force),units=options(time="s",force="N"),heatmap_parameters=[1,2],width=320,height=240,filename_template="{prefix}_{kind}_{run}.png"));
assert(out.written==9);assert(out.failures==0);
)",options);
        check(std::filesystem::exists(options.dataDirectory/"plots/candidate_heatmap_0.png"),"Parameter heatmap uses supplied safe filename template");
        run("ode d(t,real x;real a){x'=-a*x;}record b=basins(d,[1],options(parameters=options(a=searchset([1,2]))));",options,false);
        run("ode d(t,real x){x'=-x;}record e=discover(d,[1],options(t1=0.1));record o=exportexperiment(d,e,options(ids=[1],filename_template=\"../{kind}.png\"));",options,false);
        std::size_t liveFrames=0;options.figureProgress=[&](const std::vector<runtime::FigureAction>& actions){if(!actions.empty())++liveFrames;};
        run("ode d(t,real x){x'=-x;}record e=discover(d,[1;2],options(t1=0.1,sample_step=0.1,batch_size=1,live_plot=1));",options);check(liveFrames>0,"Discovery publishes throttled completed-run progress without mutating scientific results");liveFrames=0;
        run("ode d(t,real x;real rate){x'=-rate*x;}record l=lyapunov(d,1.0,options(parameters=options(rate=1.0),t1=0.1,qr_step=0.05,live_plot=1));",options);check(liveFrames>0,"Native Lyapunov state samples publish live trajectory frames");
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" native discovery workflow checks passed.\n";
}
