#include "inference/inference_module.hpp"
#ifndef PENTA_INFERENCE_KERNEL_ONLY
#ifndef PENTA_INFERENCE_LOADER_ONLY
#include "runtime/interpreter.hpp"
#endif
#include "runtime/data_io.hpp"
#else
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <future>
#include <map>
#include <mutex>
#include <condition_variable>
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
    unsigned n=2;bool domain=false,cancelAtSlope=false,sumParameters=false;std::atomic_uint64_t calls{0};std::shared_ptr<std::atomic_bool> cancellation;
    static int evaluate(void* raw,const double* theta,uint64_t n,double x,double* value,double* jac,char*,uint64_t){auto& m=*static_cast<Model*>(raw);const auto call=m.calls.fetch_add(1);if(m.cancellation&&((m.cancelAtSlope&&n==2&&std::abs(theta[1]-2.9)<1e-10)||(!m.cancelAtSlope&&call>10)))m.cancellation->store(true);if(m.domain&&theta[0]<-50)return 1;*value=m.sumParameters?theta[0]+theta[1]:n==2?theta[0]+theta[1]*x:theta[0]*x;if(jac){jac[0]=n==2?1:x;if(n==2)jac[1]=m.sumParameters?1:x;}return 0;}
};
struct Problem {
    std::vector<double> initial,starts,x,y,predict,grid;penta_inference_request_v1 request{};
    Problem(Model& model,unsigned operation=0,unsigned n=2):initial(n,0),x(20),y(20),predict{-1,0,4,10},grid{2.8,2.9,3,3.1,3.2}{model.n=n;for(unsigned i=0;i<x.size();++i){x[i]=static_cast<double>(i)/2;y[i]=(n==2?2:0)+3*x[i]+.1*std::sin(1.3*x[i]);}const auto runs=operation==0?8u:operation==1?5u:32u;starts.resize(runs*n);for(unsigned row=0;row<runs;++row)for(unsigned j=0;j<n;++j)starts[row*n+j]=static_cast<double>(row)-4;
        auto& r=request;r.struct_size=sizeof(r);r.operation=operation;r.dimension=n;r.observations=x.size();r.prediction_count=predict.size();r.runs=runs;r.seed=123;r.batch_size=8;r.initial=initial.data();r.starts=starts.data();r.x=x.data();r.y=y.data();r.prediction_x=predict.data();r.profile_coordinate=n-1;r.profile_grid=grid.data();r.confidence=.95;r.max_wall_seconds=30;r.block_length=3;r.model_context=&model;r.model=Model::evaluate;auto& f=r.fit;f.struct_size=sizeof(f);f.dimension=n;f.residual_count=x.size();f.method=1;f.loss_scale=1;f.max_iterations=100;f.max_evaluations=1000;f.threads=1;f.function_tolerance=f.gradient_tolerance=f.parameter_tolerance=1e-10;f.maximum_wall_seconds=10;f.noise_variance=-1;
    }
};
#ifdef PENTA_INFERENCE_KERNEL_ONLY
class Runner {
    const penta_inference_api_v1* api_;const penta_fit_api_v1* fit_;
    static void* library(const char* name){
#ifdef _WIN32
        wchar_t path[32768];const auto n=GetModuleFileNameW(nullptr,path,32768);const auto file=std::filesystem::path(std::wstring(path,n)).parent_path()/"modules"/(std::string(name)+".dll");auto handle=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw std::runtime_error("Cannot load isolated inference/fit DLL: "+std::to_string(GetLastError()));return handle;
#else
        return dlopen((std::string("./modules/lib")+name+".so").c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
    }
    static void* symbol(void* handle,const char* name){
#ifdef _WIN32
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle),name));
#else
        return dlsym(handle,name);
#endif
    }
    struct Host {Runner& runner;penta::inference::Result& result;penta::runtime::ParallelSettings settings;std::shared_ptr<std::atomic_bool> flag;};
    static int cancelled(void* raw){const auto& h=*static_cast<Host*>(raw);return h.flag&&h.flag->load();}
    static int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char*,uint64_t){auto& h=*static_cast<Host*>(raw);std::string error;return penta::runtime::ThreadBudget::instance().run(count,cost,task,data,h.settings,h.flag,h.result.parallel,error);}
    static int fit(void* raw,const penta_fit_request_v1* request,const penta_inference_fit_metadata_v1*,penta_fit_result_v1* output,char* error,uint64_t capacity){auto& h=*static_cast<Host*>(raw);penta_fit_host_v1 host{sizeof(host),PENTA_FIT_ABI_V1,&h,cancelled,nullptr,nullptr};return h.runner.fit_->solve(&host,request,output,error,capacity);}
public:
    Runner(){auto i=library("penta_inference"),f=library("penta_fit");api_=reinterpret_cast<penta_inference_query_function_v1>(symbol(i,"penta_inference_query_v1"))(PENTA_INFERENCE_ABI_V1);fit_=reinterpret_cast<penta_fit_query_function_v1>(symbol(f,"penta_fit_query_v1"))(PENTA_FIT_ABI_V1);}
    penta::inference::Result run(const penta_inference_request_v1& q,std::size_t threads=1){const uint64_t n=q.dimension,k=q.prediction_count,runs=q.runs;auto result=penta::inference::Result{};result.parameters.resize(runs*n);result.costs.resize(runs);result.predictions.resize(runs*k);result.baseParameters.resize(n);result.basePredictions.resize(q.observations);result.status.resize(runs);result.iterations.resize(runs);result.evaluations.resize(runs);result.ranks.resize(runs);result.messages.resize(runs*512);result.parameterMean.resize(n);result.parameterStd.resize(n);result.correlation.resize(n*n);result.correlationDefined.resize(n*n);result.fittedLower.resize(k);result.fittedUpper.resize(k);auto& r=result.report;r.struct_size=sizeof(r);r.capacity=runs;r.prediction_capacity=runs*k;r.parameters=result.parameters.data();r.costs=result.costs.data();r.predictions=result.predictions.data();r.base_parameters=result.baseParameters.data();r.base_predictions=result.basePredictions.data();r.status=result.status.data();r.iterations=result.iterations.data();r.evaluations=result.evaluations.data();r.ranks=result.ranks.data();r.messages=result.messages.data();r.parameter_mean=result.parameterMean.data();r.parameter_std=result.parameterStd.data();r.correlation=result.correlation.data();r.correlation_defined=result.correlationDefined.data();r.fitted_lower=result.fittedLower.data();r.fitted_upper=result.fittedUpper.data();Host context{*this,result,{},{}};context.settings.threads=threads;context.settings.serialThreshold=1;penta_inference_host_v1 host{sizeof(host),PENTA_INFERENCE_ABI_V1,&context,cancelled,parallel,fit,nullptr,nullptr};char error[2048]{};int status=0;penta::runtime::ThreadBudget::instance().withReservation([&]{status=api_->run(&host,&q,&r,error,sizeof(error));});if(status)throw std::runtime_error(error);return result;}
};
#else
class Runner {penta::inference::Module module_;public:penta::inference::Result run(const penta_inference_request_v1& request,std::size_t threads=1){penta::inference::Options options;options.threads=threads;options.modelIdentity="test-native-linear-v1";return module_.run(request,options);}};
#endif
void threadBudgetPumpTests(){
    struct Job {
        const std::thread::id owner=std::this_thread::get_id();std::mutex mutex;std::condition_variable condition;
        std::atomic_size_t waiting{0},done{0},pumps{0},nested{0};bool released=false;
        static int work(void* raw,uint64_t,uint64_t,char*,uint64_t){auto& j=*static_cast<Job*>(raw);if(std::this_thread::get_id()==j.owner){while(!j.waiting.load())std::this_thread::yield();return 0;}j.waiting.fetch_add(1);std::unique_lock lock(j.mutex);j.condition.wait(lock,[&]{return j.released;});j.done.fetch_add(1);return 0;}
        static int inlineWork(void* raw,uint64_t begin,uint64_t end,char*,uint64_t){auto& j=*static_cast<Job*>(raw);if(penta::runtime::ThreadBudget::inNumericalTask()&&penta::runtime::ThreadBudget::backendThreadLimit(8)==1)j.nested.fetch_add(end-begin);return 0;}
        void pump(){++pumps;penta::runtime::ParallelSettings settings;settings.threads=8;settings.serialThreshold=1;penta::runtime::ParallelStatistics stats;std::string error;if(penta::runtime::ThreadBudget::instance().run(37,4096,inlineWork,this,settings,{},stats,error))throw std::runtime_error(error);std::lock_guard lock(mutex);released=true;condition.notify_all();}
    };
    auto runJob=[](){Job j;penta::runtime::ParallelSettings settings;settings.threads=4;settings.serialThreshold=1;penta::runtime::ParallelStatistics stats;std::string error;int status=0;auto& pool=penta::runtime::ThreadBudget::instance();pool.withOwnerPump([&]{j.pump();},[&]{pool.withReservation([&]{status=pool.run(8,4096,Job::work,&j,settings,{},stats,error);});});return status==0&&j.pumps>0&&j.done>0&&j.nested>=37&&stats.threadsUsed>1&&!penta::runtime::ThreadBudget::inNumericalTask();};
    require(runJob(),"owner join pump services blocked workers and nested native math inline");
    auto a=std::async(std::launch::async,runJob),b=std::async(std::launch::async,runJob);require(a.get()&&b.get(),"simultaneous caller pumps remain owner-local under shared CPU budget");
    auto& pool=penta::runtime::ThreadBudget::instance();bool restored=false;
    pool.withOwnerPump([&]{restored=true;},[&]{try{pool.withOwnerPump([]{throw std::runtime_error("inner");},[]{throw std::runtime_error("unwind");});}catch(const std::exception&){};});
    // Scope restoration also applies after exceptions; a later ordinary job has
    // no inherited service and retains its own state.
    require(!restored&&runJob(),"nested pump scopes unwind and later pool jobs recover");
    struct Slow {std::thread::id owner=std::this_thread::get_id();std::atomic_size_t workers{0},finished{0};static int work(void* raw,uint64_t,uint64_t,char*,uint64_t){auto& s=*static_cast<Slow*>(raw);if(std::this_thread::get_id()==s.owner){while(!s.workers.load())std::this_thread::yield();}else{++s.workers;std::this_thread::sleep_for(std::chrono::milliseconds(5));++s.finished;}return 0;}}slow;
    bool failed=false;try{penta::runtime::ParallelSettings settings;settings.threads=4;settings.serialThreshold=1;penta::runtime::ParallelStatistics stats;std::string error;pool.withOwnerPump([]{throw std::runtime_error("pump failure");},[&]{pool.run(8,4096,Slow::work,&slow,settings,{},stats,error);});}catch(const std::exception& e){failed=std::string(e.what())=="pump failure";}
    require(failed&&slow.workers==slow.finished&&runJob(),"throwing pump joins every worker before propagating and releasing CPU budget");
}
void nativeTests(){
    Runner runner;
    {Model model;Problem p(model);const auto a=runner.run(p.request,1),b=runner.run(p.request,4);require(a.status==b.status&&a.parameters==b.parameters&&a.costs==b.costs,"multistart independent native fit determinism");require(a.report.successful==8&&a.report.completed==8&&a.report.best_task>=1,"multistart task identities/counts");double xm=0,ym=0,numerator=0,denominator=0;for(std::size_t i=0;i<p.x.size();++i){xm+=p.x[i]/p.x.size();ym+=p.y[i]/p.y.size();}for(std::size_t i=0;i<p.x.size();++i){numerator+=(p.x[i]-xm)*(p.y[i]-ym);denominator+=(p.x[i]-xm)*(p.x[i]-xm);}const auto slope=numerator/denominator,intercept=ym-slope*xm;for(std::size_t row=0;row<p.request.runs;++row)require(std::abs(a.parameters[row*2]-intercept)<1e-7&&std::abs(a.parameters[row*2+1]-slope)<1e-7,"Ceres multistart independent linear regression reference");require(b.parallel.threadsUsed>=1&&b.parallel.threadsUsed<=4,"small native jobs report actual bounded worker participation");model.domain=true;p.starts[0]=-100;const auto isolated=runner.run(p.request,4);require(isolated.status[0]==PENTA_INF_FAILED&&isolated.report.successful==7,"one model-domain failure is isolated");}
    {Model model;Problem p(model);p.x.resize(512);p.y.resize(512);for(unsigned i=0;i<512;++i){p.x[i]=static_cast<double>(i)/50;p.y[i]=2+3*p.x[i]+.1*std::sin(p.x[i]);}p.starts.resize(64*2);for(unsigned i=0;i<64;++i){p.starts[i*2]=-5+static_cast<double>(i)/4;p.starts[i*2+1]=1;}p.request.x=p.x.data();p.request.y=p.y.data();p.request.starts=p.starts.data();p.request.observations=512;p.request.runs=64;p.request.batch_size=16;const auto r=runner.run(p.request,4);require(r.parallel.threadsUsed>1&&r.parallel.threadsUsed<=4,"substantial independent refits actually use multiple bounded workers");}
    {Model model;Problem p(model,1);p.request.fit.noise_variance=.01;const auto result=runner.run(p.request,4);double xm=0,ym=0,xx=0;for(std::size_t i=0;i<p.x.size();++i){xm+=p.x[i]/p.x.size();ym+=p.y[i]/p.y.size();}for(const auto x:p.x)xx+=(x-xm)*(x-xm);for(std::size_t row=0;row<p.grid.size();++row){const double slope=p.grid[row],intercept=ym-slope*xm;require(std::abs(result.parameters[row*2]-intercept)<1e-7&&result.parameters[row*2+1]==slope,"profile nuisance intercept refitted without equal bounds");const double expected=result.report.base_cost+xx*std::pow(slope-result.baseParameters[1],2)/(.02);require(std::abs(result.costs[row]-expected)<1e-6,"known-variance profile objective/deviance reference");}}
    {Model model;Problem p(model,1,1);const auto result=runner.run(p.request,4);require(result.report.successful==5,"one-parameter profile directly evaluates no-nuisance objective");for(unsigned row=0;row<5;++row){double cost=0;for(unsigned i=0;i<p.x.size();++i)cost+=.5*std::pow(p.grid[row]*p.x[i]-p.y[i],2);require(std::abs(result.costs[row]-cost)<1e-9,"no nuisance profile analytic SSE");}}
    {Model model;model.sumParameters=true;Problem p(model,1);const auto r=runner.run(p.request,4);require(r.report.base_rank==1,"deliberately unidentifiable baseline reports rank deficiency");for(const auto cost:r.costs)require(std::abs(cost-r.report.base_cost)<1e-7,"unidentifiable profile is flat after nuisance refits");Problem b(model,2);const auto bootstrap=runner.run(b.request,4);require(bootstrap.report.successful==b.request.runs&&!bootstrap.report.statistics_valid,"rank-deficient bootstrap does not invent identifiable parameter uncertainty");}
    for(const unsigned method:{0u,1u,2u}){Model model;Problem p(model,2);p.request.bootstrap_method=method;const auto a=runner.run(p.request,1),b=runner.run(p.request,4);require(a.status==b.status&&a.parameters==b.parameters&&a.predictions==b.predictions,"bootstrap deterministic task seeds/worker schedules");require(a.report.statistics_valid&&a.report.successful==p.request.runs,"bootstrap successful refits counted");require(a.parameterStd[0]>0&&a.parameterStd[1]>0&&a.correlation[1]<-.5,"empirical parameter spread/correlation");for(unsigned j=0;j<p.predict.size();++j)require(std::isfinite(a.fittedLower[j])&&a.fittedLower[j]<a.fittedUpper[j],"pointwise fitted-mean percentile band");}
}
#ifndef PENTA_INFERENCE_KERNEL_ONLY
void observerTests(){
    penta::inference::Module module;auto& pool=penta::runtime::ThreadBudget::instance();
    for(unsigned operation:{0u,1u,2u}){
        Model model;Problem p(model,operation);p.request.runs=operation==1?5:8;
        penta::inference::Options options;options.threads=4;options.modelIdentity="observer-test-model";
        const auto reference=module.run(p.request,options);const auto before=model.calls.load();std::map<uint64_t,unsigned> init,iteration,done;bool ownerOkay=true,shapeOkay=true,nestedOkay=true;const auto owner=std::this_thread::get_id();
        options.observer=[&](const penta_iteration_snapshot_v1& snap){ownerOkay&=std::this_thread::get_id()==owner;shapeOkay&=snap.dimension==2&&snap.start_index<=p.request.runs;if(operation==1&&snap.start_index)shapeOkay&=snap.x[1]==p.grid[snap.start_index-1];if(snap.phase==PENTA_ITERATION_INIT)++init[snap.start_index];else if(snap.phase==PENTA_ITERATION_DONE)++done[snap.start_index];else ++iteration[snap.start_index];
            struct Math {uint64_t sum=0;static int run(void* raw,uint64_t a,uint64_t b,char*,uint64_t){auto& m=*static_cast<Math*>(raw);for(auto i=a;i<b;++i)m.sum+=i;return 0;}}math;
            penta::runtime::ParallelSettings settings;settings.threads=8;settings.serialThreshold=1;penta::runtime::ParallelStatistics stats;std::string error;
            if(snap.start_index)nestedOkay&=pool.inNumericalTask()&&pool.backendThreadLimit(8)==1;
            // Baseline is outside an outer numerical pool. Use a serial native
            // kernel there; catalogue observers must use the active reserved slot.
            if(!snap.start_index)settings.threads=1;
            nestedOkay&=pool.run(100,4096,Math::run,&math,settings,{},stats,error)==0&&math.sum==4950;return false;};
        const auto observed=module.run(p.request,options);const auto after=model.calls.load()-before;
        require(ownerOkay&&shapeOkay&&nestedOkay,"all refit observers run on owner with complete physical coordinates and safe native math");
        require(init.size()==p.request.runs+(operation?1:0)&&init==done&&!iteration.empty(),"real per-refit INIT ITER DONE lifecycle and stable task IDs");
        require(observed.parameters==reference.parameters&&observed.costs==reference.costs&&observed.evaluations==reference.evaluations&&after==before,"nonmutating observer adds no hidden native model evaluations");
    }
    Model model;Problem p(model);penta::inference::Options options;options.threads=4;options.modelIdentity="observer-stop-test";
    options.observer=[](const penta_iteration_snapshot_v1& s){return s.phase==PENTA_ITERATION_ITER;};const auto stopped=module.run(p.request,options);require(stopped.report.user_stopped&&!stopped.report.cancelled&&stopped.report.completed<p.request.runs,"OutputFcn clean stop is distinct from cancel/convergence and preserves pending tasks");
    options.observer=[](const penta_iteration_snapshot_v1& s)->bool{if(s.phase==PENTA_ITERATION_ITER)throw std::runtime_error("intentional observer failure");return false;};bool threw=false;try{module.run(p.request,options);}catch(const std::exception& e){threw=std::string(e.what())=="intentional observer failure";}require(threw,"observer exception preserved after every fit worker joins");
    auto cancellation=std::make_shared<std::atomic_bool>(false);options.cancellation=cancellation;options.observer=[&](const penta_iteration_snapshot_v1& s){if(s.phase==PENTA_ITERATION_ITER)cancellation->store(true);return false;};const auto cancelled=module.run(p.request,options);require(cancelled.report.cancelled&&!cancelled.report.user_stopped,"external cancellation releases queued observers without becoming clean stop");
    options.cancellation={};options.observer={};require(module.run(p.request,options).report.successful==p.request.runs,"CPU budget and fit callbacks recover after stop/failure/cancellation");
}
void checkpointTests(){
    const auto directory=std::filesystem::temp_directory_path()/("penta-refit-ledger-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);Model model;Problem p(model,1);p.request.batch_size=2;model.cancelAtSlope=true;auto flag=std::make_shared<std::atomic_bool>(false);model.cancellation=flag;
    penta::inference::Module module;penta::inference::Options options;options.threads=1;options.cancellation=flag;options.checkpoint=directory/"profile.json";options.modelIdentity="unchanged-native-linear-model";const auto partial=module.run(p.request,options);if(!partial.checkpointOkay)std::cerr<<"Checkpoint diagnostic: "<<partial.checkpointMessage<<" path="<<options.checkpoint<<'\n';require(partial.report.cancelled&&partial.checkpointOkay&&std::filesystem::exists(options.checkpoint),"interrupted refits retain an atomic completed-task checkpoint");
    flag->store(false);model.cancellation.reset();options.threads=4;options.resume=true;options.retryFailed=true;const auto resumed=module.run(p.request,options);require(resumed.restored>=1&&resumed.report.successful==p.request.runs,"resume reuses complete tasks and restarts interrupted fits");Runner runner;const auto reference=runner.run(p.request,1);require(resumed.parameters==reference.parameters&&resumed.costs==reference.costs,"resumed scientific fields equal uninterrupted reference");
    const auto bytes=penta::runtime::dataio::readBytes(options.checkpoint);auto corrupt=bytes;const auto position=corrupt.find("\"version\":1");require(position!=std::string::npos,"checkpoint version marker present");corrupt.replace(position,11,"\"version\":9");penta::runtime::dataio::writeBytesAtomic(options.checkpoint,corrupt);rejects([&]{module.run(p.request,options);},"incompatible checkpoint version rejected");require(penta::runtime::dataio::readBytes(options.checkpoint)==corrupt,"rejected checkpoint retained byte for byte");penta::runtime::dataio::writeBytesAtomic(options.checkpoint,bytes);p.request.confidence=.9;rejects([&]{module.run(p.request,options);},"changed scientific settings reject resume");require(penta::runtime::dataio::readBytes(options.checkpoint)==bytes,"incompatible settings preserve prior result ledger");std::error_code ignored;std::filesystem::remove_all(directory,ignored);
}
#ifndef PENTA_INFERENCE_LOADER_ONLY
void languageTests(){
    penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-inference-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";
    auto run=[&](const std::string& source,bool good=true){const auto result=penta::runtime::execute(source,{},options);require(result.ok()==good,"compiled inference language case");if(result.ok()!=good){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}};
    const std::string line="native function real line(real intercept,real slope,real x){return intercept+slope*x;}matrix x=0:9;matrix y=2+3*x+.1*sin(x);";
    run(line+"record a=multistartfit(line,[-2,1;0,0;3,5],x,y,options(threads=4));assert(a.successful==3&&a.inner_threads==1);record b=profilefit(line,[0,0],x,y,2,[2.8,3,3.2],options(objective=\"gaussian_known_variance\",noise_variance=.01,threads=4));assert(b.cache_hit&&a.cache_key==b.cache_key);assert(b.profile_valid(2)==1&&b.deviance(1)>b.deviance(2));");
    run(line+"record b=fitbootstrap(line,[0,0],x,y,options(samples=16,sample_method=\"residual\",prediction_x=[0,3,8],seed=1,threads=4));assert(b.successful==16&&b.statistics_valid);assert(b.band_kind==\"pointwise_fitted_mean_percentile\"&&size(b.fitted_lower,1)==3);");
    run(line+"record b=fitbootstrap(line,[0,0],x,y,options(samples=16,sample_method=\"moving_block\",block_length=3,seed=1,threads=4));assert(b.statistics_valid&&b.block_length==3);");
    run("native function real model(pentarnion p,real x){return coeff(p,0)+coeff(p,1)*x+coeff(p,2)*x^2+coeff(p,3)*x^3+coeff(p,4)*x^4;}matrix x=linspace(0,1,12);matrix y=1+x+x.^2+x.^3+x.^4;record r=multistartfit(model,[0,0,0,0,0;2,2,2,2,2],x,y,options(threads=2));assert(r.successful==2&&norm(r.best_parameters-ones(1,5),2)<1e-5);");
    bool ownerPlots=true;std::size_t frames=0;const auto owner=std::this_thread::get_id();options.figureProgress=[&](const auto& frame){ownerPlots&=std::this_thread::get_id()==owner;if(!frame.empty())++frames;};
    const std::string monitor="function integer monitor(matrix point,record info,string state){matrix a=ones(1,40000);matrix b=a+a;assert(b(1)==2&&b(40000)==2);figure(59);plot([0,1],[point(1),point(1)+point(2)]);return 0;}";
    // Large observer arrays consume the same VM work budget as normal code.
    // First prove exhaustion releases all workers, then allow this stress case.
    run(line+monitor+"record b=multistartfit(line,[-2,1;0,0;3,5;1,2],x,y,options(threads=4,OutputFcn=monitor,OutputEvery=2));",false);
    const auto ordinaryBudget=options.maxOperations;options.maxOperations=8000000;
    run(line+monitor+"record b=multistartfit(line,[-2,1;0,0;3,5;1,2],x,y,options(threads=4,OutputFcn=monitor,OutputEvery=2));assert(b.successful==4&&b.user_stopped==0);");
    options.maxOperations=ordinaryBudget;
    require(ownerPlots&&frames>0,"DSL observer plots publish on the owner while parallel native array kernels remain safe");options.figureProgress={};
    run(line+"function integer stopfit(matrix point,record info,string state){if(state==\"iter\"){return 1;}return 0;}record b=fitbootstrap(line,[0,0],x,y,options(samples=8,threads=4,OutputFcn=stopfit));assert(b.user_stopped==1&&b.status==\"output_function_stop\"&&b.completed<8);");
    run(line+"function integer badfit(matrix point,record info,string state){if(state==\"iter\"){error(\"intentional DSL observer failure\");}return 0;}record b=multistartfit(line,[0,0;1,1;2,2;3,3],x,y,options(threads=4,OutputFcn=badfit));",false);
    run(line+"record b=multistartfit(line,[0,0;1,1],x,y,options(threads=2));assert(b.successful==2);");
    run(line+"record b=profilefit(line,[0,0],x,y,2,[2.8,3,3.2],options(objective=\"gaussian_known_variance\"));",false);
    run(line+"record b=fitbootstrap(line,[0,0],x,y,options(sample_method=\"moving_block\"));",false);
    run(line+"record b=multistartfit(line,[0,0],x,y,options(max_native_operations=1));",false);
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
#endif
}
int main(){try{threadBudgetPumpTests();nativeTests();
#ifndef PENTA_INFERENCE_KERNEL_ONLY
checkpointTests();observerTests();
#ifndef PENTA_INFERENCE_LOADER_ONLY
languageTests();
#endif
#endif
}catch(const std::exception& failure){++failures;std::cerr<<"UNEXPECTED: "<<failure.what()<<'\n';}std::cout<<checks<<" inference checks, "<<failures<<" failures\n";return failures?1:0;}
