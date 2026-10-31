#include "pde/pde_experiment_module.hpp"
#include "runtime/lazy_module.hpp"
#ifndef PENTA_PDE_EXPERIMENT_KERNEL_ONLY
#ifndef PENTA_PDE_EXPERIMENT_LOADER_ONLY
#include "runtime/interpreter.hpp"
#endif
#include "runtime/data_io.hpp"
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
struct Reaction {
    unsigned components=1;std::vector<double> rates,gammas;bool domain=false;int cancelRow=-1;std::shared_ptr<std::atomic_bool> flag;
    static int evaluate(void* raw,uint64_t row,double,double,double,const double* state,double* out,char*,uint64_t){auto& r=*static_cast<Reaction*>(raw);if(r.domain&&row==2)return 1;if(r.flag&&r.cancelRow==static_cast<int>(row))r.flag->store(true);for(unsigned j=0;j<r.components;++j)out[j]=-(r.rates[row]+r.gammas[row])*state[j];return 0;}
};
penta::pde::experiments::Options options(Reaction& reaction,unsigned c=1,bool grid=false,unsigned method=0,unsigned integrator=0){
    penta::pde::experiments::Options o;o.grid=grid;o.runs=8;o.batchSize=8;o.maximumWallSeconds=30;o.modelIdentity="native_decay_test_v1";o.x.resize(grid?17:65);if(grid)o.y.resize(13);for(std::size_t i=0;i<o.x.size();++i)o.x[i]=static_cast<double>(i)/o.x.size();for(std::size_t i=0;i<o.y.size();++i)o.y[i]=static_cast<double>(i)/o.y.size();const auto nodes=o.x.size()*(grid?o.y.size():1);o.initial.resize(o.runs*nodes*c);reaction.components=c;reaction.rates.resize(o.runs);reaction.gammas.resize(o.runs);for(std::size_t row=0;row<o.runs;++row){reaction.rates[row]=.1+.02*row;reaction.gammas[row]=.1+.1*row;for(std::size_t i=0;i<nodes;++i)for(unsigned j=0;j<c;++j)o.initial[(row*nodes+i)*c+j]=.5+.02*row+.01*j;}
    o.reactionContext=&reaction;o.reaction=Reaction::evaluate;
    auto& a=o.one.base;a.struct_size=sizeof(o.one);o.one.integrator=integrator;a.method=method;a.boundary=0;a.components=c;a.nodes=nodes;a.length=1;a.diffusion=.01;a.t1=.01;a.time_step=.0001;a.sample_step=.005;a.max_steps=100000;a.max_reaction_evaluations=10000000;a.max_wall_seconds=10;
    auto& b=o.two;b.struct_size=sizeof(b);b.abi_version=PENTA_PDE_GRID_ABI_V1;b.integrator=integrator;b.components=c;b.nx=o.x.size();b.ny=o.y.size();b.length_x=b.length_y=1;b.diffusion_x=b.diffusion_y=.01;b.t1=.01;b.time_step=.0001;b.sample_step=.005;b.max_steps=100000;b.max_reaction_evaluations=10000000;b.max_wall_seconds=10;return o;
}
#ifdef PENTA_PDE_EXPERIMENT_KERNEL_ONLY
class Runner {
    const penta_pde_experiment_api_v1 api_=penta::runtime::detail::NativeModuleRegistry<penta_pde_experiment_api_v1>::get(penta::runtime::detail::nativeModulePath({},"penta_pde_experiments"),"penta_pde_experiment_query_v1",PENTA_PDE_EXPERIMENT_ABI_V1);
    struct Context{const penta::pde::experiments::Options& options;penta::pde::experiments::Result& result;penta::pde::Module solver;penta::runtime::ParallelSettings settings;};
    struct Reaction{const penta::pde::experiments::Options& options;uint64_t row;static int one(void* raw,double t,double x,const double* u,double* out,char* error,uint64_t cap){auto& r=*static_cast<Reaction*>(raw);return r.options.reaction(r.options.reactionContext,r.row,t,x,0,u,out,error,cap);}static int two(void* raw,double t,double x,double y,const double* u,double* out,char* error,uint64_t cap){auto& r=*static_cast<Reaction*>(raw);return r.options.reaction(r.options.reactionContext,r.row,t,x,y,u,out,error,cap);}};
    static int cancelled(void* raw){const auto& c=*static_cast<Context*>(raw);return c.options.cancellation&&c.options.cancellation->load();}
    static int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char*,uint64_t){auto& c=*static_cast<Context*>(raw);std::string error;return penta::runtime::ThreadBudget::instance().run(count,cost,task,data,c.settings,c.options.cancellation,c.result.parallel,error);}
    static int solve(void* raw,uint64_t row,penta_pde_run_v1* out,char* error,uint64_t capacity){auto& c=*static_cast<Context*>(raw);try{const auto& o=c.options;const uint64_t n=o.grid?o.two.nx*o.two.ny:o.one.base.nodes,width=o.grid?o.two.components:o.one.base.components;Reaction reaction{o,row};auto owner=std::make_shared<penta::pde::Result>();if(o.grid){auto q=o.two;q.x=o.x.data();q.y=o.y.data();q.initial=o.initial.data()+row*n*width;q.boundary_value=o.boundary.data();q.reaction_context=&reaction;q.reaction=Reaction::two;*owner=c.solver.solveGrid(q,o.cancellation,o.charge,1);}else{auto q=o.one;q.base.x=o.x.data();q.base.initial=o.initial.data()+row*n*width;q.base.left=o.left.data();q.base.right=o.right.data();q.base.reaction_context=&reaction;q.base.reaction=Reaction::one;*owner=c.solver.solve(q.base,o.cancellation,o.charge,1);}if(!o.retain.empty()&&o.retain[row])c.result.retained[row]=owner;
        const auto& r=owner->report;out->samples=r.samples;out->nodes=n;out->components=width;out->steps=r.steps;out->reaction_evaluations=r.reaction_evaluations;out->linear_solves=owner->linearSolves;out->factorizations=owner->factorizations;out->threads_used=owner->statistics.threadsUsed;out->times=owner->times.data();out->values=owner->values.data();out->means=owner->means.data();out->rms=owner->rms.data();out->handle=new std::shared_ptr<penta::pde::Result>(owner);return 0;}catch(const penta::pde::Error& e){if(error&&capacity){const std::string message=e.what();const auto size=std::min<std::size_t>(message.size(),capacity-1);std::copy_n(message.data(),size,error);error[size]=0;}return e.status==3?3:1;}}
    static void release(void*,penta_pde_run_v1* run){delete static_cast<std::shared_ptr<penta::pde::Result>*>(run->handle);run->handle=nullptr;}
public:
    penta::pde::experiments::Result run(const penta::pde::experiments::Options& o,std::size_t threads){const uint64_t nodes=o.grid?o.two.nx*o.two.ny:o.one.base.nodes,c=o.grid?o.two.components:o.one.base.components,runs=o.runs;penta::pde::experiments::Result result;result.status.resize(runs);result.finalValues.resize(runs*nodes*c);result.finalMeans.resize(runs*c);result.finalRms.resize(runs);result.maximumRms.resize(runs);result.stopTimes.resize(runs);result.steps.resize(runs);result.evaluations.resize(runs);result.linearSolves.resize(runs);result.factorizations.resize(runs);result.innerThreads.resize(runs);result.messages.resize(runs*512);result.retained.resize(runs);auto& r=result.report;r.struct_size=sizeof(r);r.capacity=runs;r.status=result.status.data();r.final_values=result.finalValues.data();r.final_means=result.finalMeans.data();r.final_rms=result.finalRms.data();r.maximum_rms=result.maximumRms.data();r.stop_times=result.stopTimes.data();r.steps=result.steps.data();r.evaluations=result.evaluations.data();r.linear_solves=result.linearSolves.data();r.factorizations=result.factorizations.data();r.inner_threads=result.innerThreads.data();r.messages=result.messages.data();Context context{o,result,penta::pde::Module(),{}};context.settings.threads=threads;context.settings.serialThreshold=1;penta_pde_experiment_host_v1 host{sizeof(host),PENTA_PDE_EXPERIMENT_ABI_V1,&context,cancelled,parallel,solve,release,nullptr};penta_pde_experiment_request_v1 q{sizeof(q),static_cast<uint32_t>(c),runs,nodes,o.batchSize,o.maximumWallSeconds};char error[2048]{};int status=0;penta::runtime::ThreadBudget::instance().withReservation([&]{status=api_.run(&host,&q,&r,error,sizeof(error));});if(status)throw std::runtime_error(error);return result;}
};
#else
class Runner{penta::pde::experiments::Module module_;public:penta::pde::experiments::Result run(penta::pde::experiments::Options o,std::size_t threads){o.threads=threads;return module_.run(o);}};
#endif
void nativeTests(){
    Runner runner;
    for(const bool grid:{false,true})for(const unsigned c:{1u,5u})for(const unsigned integrator:{0u,1u}){
        Reaction reaction;auto o=options(reaction,c,grid,0,integrator);o.retain.assign(o.runs,0);o.retain[0]=o.retain[3]=1;const auto serial=runner.run(o,1),four=runner.run(o,4),eight=runner.run(o,8);require(serial.finalValues==four.finalValues&&serial.finalValues==eight.finalValues&&serial.finalMeans==eight.finalMeans,"PDE batch serial/4/8 scientific fields agree exactly");require(serial.status==four.status&&serial.steps==eight.steps&&serial.evaluations==eight.evaluations,"PDE batch statuses/counts independent of schedule");require(eight.report.completed==o.runs&&std::all_of(eight.innerThreads.begin(),eight.innerThreads.end(),[](auto n){return n==1;}),"PDE outer parallelism reserves exactly one inner CPU slot");require(four.parallel.threadsUsed>1&&four.parallel.threadsUsed<=4&&eight.parallel.threadsUsed<=8,"PDE independent runs use actual bounded outer workers");const auto nodes=o.x.size()*(grid?o.y.size():1);double maximum=0;for(std::size_t row=0;row<o.runs;++row){const double rate=reaction.rates[row]+reaction.gammas[row],amplification=integrator?std::pow(1-rate*.0001,100):std::exp(-rate*.01);for(std::size_t i=0;i<nodes*c;++i)maximum=std::max(maximum,std::abs(serial.finalValues[row*nodes*c+i]-amplification*o.initial[row*nodes*c+i]));}require(maximum<2e-12,"PDE ensemble independent exponential/Euler mode reference");require(serial.retained[0]&&serial.retained[3]&&!serial.retained[2],"only explicitly selected PDE runs retain full fields");require(serial.retained[0]->report.samples==3&&serial.retained[0]->values.back()==serial.finalValues[nodes*c-1],"retained snapshot reproduces final field");
    }
    {Reaction reaction;auto o=options(reaction,5,false,1);const auto r=runner.run(o,4);require(r.report.completed==8&&std::all_of(r.status.begin(),r.status.end(),[](auto s){return s==PENTA_PDE_RUN_COMPLETED;}),"parallel batch reuses consistent-mass P1 FE backend");}
    {Reaction reaction;reaction.domain=true;auto o=options(reaction);const auto r=runner.run(o,4);require(r.status[2]==PENTA_PDE_RUN_FAILED&&r.report.completed==8&&r.status[0]==PENTA_PDE_RUN_COMPLETED,"one PDE reaction failure is isolated by run");}
}
#ifndef PENTA_PDE_EXPERIMENT_KERNEL_ONLY
void checkpointTests(){
    const auto directory=std::filesystem::temp_directory_path()/("penta-pde-ledger-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);Reaction reaction;auto o=options(reaction);o.batchSize=2;o.threads=1;o.checkpoint=directory/"pde.json";auto flag=std::make_shared<std::atomic_bool>(false);o.cancellation=flag;reaction.flag=flag;reaction.cancelRow=2;penta::pde::experiments::Module module;const auto partial=module.run(o);require(partial.report.cancelled&&partial.report.completed==2&&partial.checkpointOkay,"PDE cancellation retains completed summaries atomically");flag->store(false);reaction.flag.reset();reaction.cancelRow=-1;o.resume=true;o.threads=4;const auto resumed=module.run(o);require(resumed.restored==2&&resumed.report.completed==8,"PDE resume skips completed solves");auto clean=o;clean.resume=false;clean.checkpoint.clear();const auto reference=module.run(clean);require(resumed.finalValues==reference.finalValues&&resumed.finalMeans==reference.finalMeans,"PDE interrupted/resumed scientific fields match reference");
    const auto bytes=penta::runtime::dataio::readBytes(o.checkpoint);o.one.base.diffusion=.02;rejects([&]{module.run(o);},"PDE checkpoint rejects changed solver settings");require(penta::runtime::dataio::readBytes(o.checkpoint)==bytes,"incompatible PDE settings preserve prior ledger");o.one.base.diffusion=.01;o.retain.assign(o.runs,0);o.retain[0]=1;rejects([&]{module.run(o);},"summary-only resume explicitly rejects retain");o.resume=false;o.checkpoint.clear();o.maximumBytes=1024;rejects([&]{module.run(o);},"PDE batch workspace memory estimate enforced");std::error_code ignored;std::filesystem::remove_all(directory,ignored);
}
#ifndef PENTA_PDE_EXPERIMENT_LOADER_ONLY
void languageTests(){
    penta::runtime::ExecutionOptions execution;execution.dataDirectory=std::filesystem::temp_directory_path()/("penta-pde-ensemble-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(execution.dataDirectory);execution.odeCacheDirectory=execution.dataDirectory/".penta-cache/odes";execution.maxOperations=20000000;
    auto run=[&](const std::string& source,bool good=true){const auto result=penta::runtime::execute(source,{},execution);require(result.ok()==good,"native PDE experiment language case");if(result.ok()!=good){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}};
    const std::string decay="native function real decay(real u,real t,real x,real rate){return -rate*u;}matrix x=(0:15)/16;matrix initial=ones(1,16);";
    run(decay+"record p=pdeensemble(decay,x,[initial;2*initial],options(scan=options(rate=[.2,.4]),gammas=[1,1],pairing=\"cartesian\",t1=.01,time_step=.001,dry_run=1));assert(p.runs==8&&p.compiled_models==0);");
    run(decay+"record b=pdeensemble(decay,x,initial,options(scan=options(rate=[.2,.4]),t1=.01,time_step=.001,sample_step=.01,diffusion=0,retain=[2],threads=4));assert(b.runs==2&&b.components==1&&b.compiled_models==1);record f=pderun(b,2);assert(norm(f.values(end,:)-b.final_values(2,:),2)==0);record p=pdeparams(b,2);assert(abs(p.rate-.4)<1e-12);record replay=pdereplay(decay,b,1);assert(norm(replay.values(end,:)-b.final_values(1,:),2)==0);record cfg=pdesettings(b,2);record single=pdesolve(decay,x,initial,cfg);assert(norm(single.values(end,:)-f.values(end,:),2)==0);");
    run(decay+"record b=pdeensemble(decay,x,[initial;2*initial],options(parameter_sets=(options(rate=.2),options(rate=.4)),gammas=[.5,1],t1=.01,time_step=.001));assert(b.runs==2&&b.source_indices(2,1)==2&&b.source_indices(2,2)==2&&b.source_indices(2,3)==2);");
    run("native function pentarnion reaction(pentarnion p,real t,real x,pentarnion A){return A*p;}matrix x=(0:7)/8;matrix initial=ones(1,8)*j;record b=pdeensemble(reaction,x,initial,options(parameters=options(A=i),gammas=[.3,1],t1=.005,time_step=.0001,sample_step=.005,diffusion=0,retain=[1,2],threads=2));assert(b.components==5&&size(b.final_values,2)==40&&b.value_layout==\"node_coefficients\");record f=pderun(b,1);assert(f.gamma==.3&&size(f.values,2)==40);record raw=pdeparams(b,1);assert(size(raw.A,2)==5&&raw.A(2)==1);matrix coefficient=pdecoeff(f,3);assert(abs(coefficient(end,1)-b.final_values(1,4))<1e-15);assert(norm(b.final_values(1,:)-b.final_values(2,:),2)>1e-5);record same=pdereplay(reaction,b,2);assert(norm(same.values(end,:)-b.final_values(2,:),2)==0);");
    run("native function pentarnion reaction(pentarnion p,real t,real x){return -p;}record b=pdeensemble(reaction,[0,.5,1],ones(1,3)*i,options(gammas=.3,boundary=\"neumann\",t1=.001,time_step=.001));record cfg=pdesettings(b,1);",false);
    run("native function pentarnion reaction(pentarnion p,real t,real x){return -p;}record b=pdeensemble(reaction,[0,.5,1],ones(1,3)*i,options(gammas=.3,boundary=\"neumann\",t1=.001,time_step=.001));record r=pdereplay(reaction,b,1);",false);
    run("native function pentarnion zero(pentarnion p,real t,real x,real y){return 0*p;}matrix x=linspace(0,1,5);matrix y=linspace(0,1,4);matrix initial=ones(4,5)*(1+2*i+3*j+4*k+5*ell);record b=pdeensemble2(zero,x,y,initial,options(gammas=[.3,1],boundary=\"neumann\",integrator=\"imex_euler\",t1=.01,time_step=.01,sample_step=.01,retain=[1],threads=2));record f=pderun(b,1);matrix z=pdeslice(f,2,4);assert(size(z,1)==4&&size(z,2)==5&&norm(z-5,\"fro\")<1e-10);");
    run("native function pentarnion zero(pentarnion p,real t,real x,real y){return 0*p;}matrix x=linspace(0,1,3);matrix y=linspace(0,1,3);record b=pdeensemble2(zero,x,y,ones(3,3)*i,options(boundary=\"neumann\",t1=.001,time_step=.001,retain=[1]));record f=pderun(b,1);matrix bad=pdeslice(f,1);",false);
    run(decay+"record b=pdeensemble(decay,x,initial,options(parameters=options(rate=.2),t1=.001,time_step=.001));record f=pderun(b,1);",false);
    run(decay+"record b=pdeensemble(decay,x,[initial;initial],options(scan=options(rate=[.1,.2,.3]),t1=.001,time_step=.001));",false);
    run(decay+"record b=pdeensemble(decay,x,initial,options(parameters=options(rate=.2),t1=.001,time_step=.001,max_native_operations=1));",false);
    std::error_code ignored;std::filesystem::remove_all(execution.dataDirectory,ignored);
}
#endif
#endif
}
int main(){try{nativeTests();
#ifndef PENTA_PDE_EXPERIMENT_KERNEL_ONLY
checkpointTests();
#ifndef PENTA_PDE_EXPERIMENT_LOADER_ONLY
languageTests();
#endif
#endif
}catch(const std::exception& failure){++failures;std::cerr<<"UNEXPECTED: "<<failure.what()<<'\n';}std::cout<<checks<<" PDE ensemble checks, "<<failures<<" failures\n";return failures?1:0;}
