#include "runtime/numeric_module_loader.hpp"
#ifndef PENTA_PARALLEL_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace {
using namespace penta::runtime;
int checks=0,failures=0;
void check(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
template<class F>void rejects(F task,int status,const std::string& label){try{task();check(false,label);}catch(const NumericModuleError& error){check(error.status==status,label+": "+error.what());}}
penta_numeric_vector_request_v1 vectorRequest(std::uint32_t operation,const std::vector<double>& a,const std::vector<double>& b,std::vector<double>& output,std::uint32_t width=1){
    penta_numeric_vector_request_v1 request{};request.struct_size=sizeof(request);request.operation=operation;request.count=a.size()/width;
    request.a=a.data();request.a_count=request.count;request.a_width=width;request.b=b.data();request.b_count=b.size()==1?1:request.count;request.b_width=1;
    request.output_width=width;request.output=output.data();request.output_capacity=output.size();request.gamma=1;return request;
}
void vectorTests(){
    NumericModule module;ParallelStatistics stats;ParallelSettings settings;settings.threads=4;
    check(!module.loaded(),"optional numerical module is initially unloaded");
    std::vector<double> a(100000),b(a.size()),output(a.size()),reference(a.size());
    for(std::size_t n=0;n<a.size();++n){a[n]=static_cast<double>(n)*.001;b[n]=static_cast<double>(n%31)-15;reference[n]=a[n]+b[n];}
    auto request=vectorRequest(PENTA_VEC_ADD,a,b,output);module.vector(request,settings,{},stats);
    check(module.loaded(),"first vector use loads the numerical module");check(output==reference,"parallel vector addition matches scalar double arithmetic");
    check(stats.parallelCalls>0&&stats.threadsUsed>1&&stats.peakActiveThreads>1,"large vector actually uses concurrent workers");
    check(stats.threadsUsed<=4&&stats.peakActiveThreads<=4,"requested CPU budget is respected");
    const auto parallelResult=output;settings.threads=1;module.vector(request,settings,{},stats);check(output==parallelResult,"entrywise arithmetic is identical at one/four threads");check(stats.threadsUsed==1,"explicit serial configuration uses one thread");
    settings.threads=4;std::vector<double> smallA{1,2,3},smallB{3,4,5},smallOutput(3);request=vectorRequest(PENTA_VEC_MULTIPLY,smallA,smallB,smallOutput);module.vector(request,settings,{},stats);
    check(smallOutput==std::vector<double>({3,8,15})&&stats.threadsUsed==1,"small vector remains serial");
    std::vector<double> scalar{.5};request=vectorRequest(PENTA_VEC_AXPY,a,b,output);request.alpha=scalar.data();request.alpha_width=1;module.vector(request,settings,{},stats);
    bool axpy=true;for(std::size_t n=0;n<a.size();++n)axpy=axpy&&output[n]==a[n]*.5+b[n];check(axpy,"AXPY uses alpha*x+y");
    std::vector<double> pent(5*20000),pure(5*20000);for(std::size_t n=0;n<20000;++n){pent[5*n]=1;pent[5*n+1]=.25;pent[5*n+2]=-.5;}
    std::vector<double> right{2};request=vectorRequest(PENTA_VEC_MULTIPLY,pent,right,pure,5);module.vector(request,settings,{},stats);
    bool scaled=true;for(std::size_t n=0;n<pent.size();++n)scaled=scaled&&pure[n]==pent[n]*2;check(scaled,"ordered Pentarnion vector right-real scaling");
    std::vector<double> q(pent.size());for(std::size_t n=0;n<20000;++n){q[5*n+1]=1;q[5*n+4]=1;}
    request.b=q.data();request.b_count=20000;request.b_width=5;module.vector(request,settings,{},stats);const auto ab=pure;
    request.a=q.data();request.b=pent.data();module.vector(request,settings,{},stats);check(pure!=ab,"Pentarnion products retain operand order");
    request=vectorRequest(PENTA_VEC_SIN,pent,{},pure,5);module.vector(request,settings,{},stats);check(std::all_of(pure.begin(),pure.end(),[](double x){return std::isfinite(x);}),"whole-Pentarnion sine kernel returns finite coefficients");
    auto cancel=std::make_shared<std::atomic_bool>(true);std::fill(output.begin(),output.end(),7);request=vectorRequest(PENTA_VEC_ADD,a,b,output);
    rejects([&]{module.vector(request,settings,cancel,stats);},PENTA_NUMERIC_CANCELLED,"pre-cancelled vector rejected");check(output.front()==7&&output.back()==7,"pre-cancel preserves output buffer");
    request.output_capacity=1;rejects([&]{module.vector(request,settings,{},stats);},PENTA_NUMERIC_ARGUMENT,"module checks output capacity");
    request=vectorRequest(PENTA_VEC_DIVIDE,smallA,std::vector<double>{0},smallOutput);std::vector<double> zero{0};request.b=zero.data();
    rejects([&]{module.vector(request,settings,{},stats);},PENTA_NUMERIC_DIVISION,"division by zero reports arithmetic failure");
    settings.threads=0;check(ThreadBudget::effectiveThreads(0)>=1&&ThreadBudget::effectiveThreads(0)<=64,"automatic CPU selection is bounded");
}
void reductions(){
    NumericModule module;ParallelSettings settings;settings.threads=1;ParallelStatistics statistics;
    std::vector<double> data(100000),other(data.size());for(std::size_t n=0;n<data.size();++n){data[n]=n%3==0?1e12:n%3==1?-1e12:.125;other[n]=.5;}
    double one=0,four=0;penta_numeric_reduce_request_v1 request{};request.struct_size=sizeof(request);request.operation=PENTA_REDUCE_SUM;request.width=1;request.count=data.size();request.a=data.data();request.output=&one;request.output_capacity=1;
    module.reduce(request,settings,{},statistics);settings.threads=4;request.output=&four;module.reduce(request,settings,{},statistics);
    check(one==four,"fixed reduction tree is identical at one/four threads");check(statistics.threadsUsed>1&&statistics.peakActiveThreads>1,"large reduction actually uses concurrent workers");
    request.operation=PENTA_REDUCE_DOT;request.b=other.data();module.reduce(request,settings,{},statistics);check(four==one*.5,"real dot product uses fixed coefficient order");
    std::vector<double> huge{1e300,1e300};request.operation=PENTA_REDUCE_NORM;request.count=huge.size();request.a=huge.data();module.reduce(request,settings,{},statistics);
    check(std::isfinite(four)&&std::abs(four/1e300-std::sqrt(2.))<1e-14,"L2 norm avoids intermediate square overflow");
    request.operation=PENTA_REDUCE_NORM_INFINITY;module.reduce(request,settings,{},statistics);check(four==1e300,"infinity norm");
    request.operation=PENTA_REDUCE_NORM_L1;module.reduce(request,settings,{},statistics);check(four==2e300,"L1 norm");
    request.operation=PENTA_REDUCE_NORM_SQUARED;rejects([&]{module.reduce(request,settings,{},statistics);},PENTA_NUMERIC_DOMAIN,"nonfinite final squared norm rejected");
    request.operation=PENTA_REDUCE_SUM;request.count=0;request.a=nullptr;module.reduce(request,settings,{},statistics);check(four==0,"empty sum is zero");
    request.operation=PENTA_REDUCE_MEAN;rejects([&]{module.reduce(request,settings,{},statistics);},PENTA_NUMERIC_DOMAIN,"empty mean rejected");
}
void loaderFailures(){
    const auto parent=std::filesystem::temp_directory_path();const auto folder=parent/("penta-module-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(folder);
    NumericModule absent(folder);check(!absent.loaded(),"absent optional module query does not load it");
    std::vector<double> a{1},b{2},result{9};auto request=vectorRequest(PENTA_VEC_ADD,a,b,result);ParallelSettings settings;ParallelStatistics statistics;
    auto cancel=std::make_shared<std::atomic_bool>(true);rejects([&]{absent.vector(request,settings,cancel,statistics);},PENTA_NUMERIC_CANCELLED,"cancel precedes optional module load");check(!absent.loaded(),"pre-cancel does not attempt a module load");
    rejects([&]{absent.vector(request,settings,{},statistics);},PENTA_NUMERIC_INTERNAL,"missing optional module gives controlled error");check(result[0]==9,"missing module leaves result storage intact");
    try{NumericModule relative("relative-module-directory");check(false,"relative module location rejected");}catch(const NumericModuleError& error){check(error.status==PENTA_NUMERIC_ARGUMENT,"relative module location rejected");}
#ifdef PENTA_BAD_NUMERIC_MODULE_DIR
    NumericModule incompatible(std::filesystem::path(PENTA_BAD_NUMERIC_MODULE_DIR));rejects([&]{incompatible.vector(request,settings,{},statistics);},PENTA_NUMERIC_VERSION,"incompatible module ABI rejected");check(!incompatible.loaded(),"incompatible module is not cached");
#else
    if(const char* directory=std::getenv("PENTA_BAD_NUMERIC_MODULE_DIR")){NumericModule incompatible{std::filesystem::path(directory)};rejects([&]{incompatible.vector(request,settings,{},statistics);},PENTA_NUMERIC_VERSION,"incompatible module ABI rejected");check(!incompatible.loaded(),"incompatible module is not cached");}
#endif
    check(std::filesystem::weakly_canonical(folder.parent_path())==std::filesystem::weakly_canonical(parent),"owned temporary module test path stays within temporary directory");std::filesystem::remove_all(folder);
}
void sharedBudget(){
    struct Counters {std::atomic_int active{0},peak{0};} counters;
    const auto callback=[](void* raw,uint64_t begin,uint64_t end,char*,uint64_t){auto& value=*static_cast<Counters*>(raw);const auto current=value.active.fetch_add(1)+1;auto maximum=value.peak.load();while(maximum<current&&!value.peak.compare_exchange_weak(maximum,current)){}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));value.active.fetch_sub(1);return begin<end?0:1;};
    ParallelSettings settings;settings.threads=3;settings.serialThreshold=1;ParallelStatistics first,second;std::string firstError,secondError;int firstStatus=1,secondStatus=1;
    std::thread a([&]{firstStatus=ThreadBudget::instance().run(10000,1,callback,&counters,settings,{},first,firstError);});
    std::thread b([&]{secondStatus=ThreadBudget::instance().run(10000,1,callback,&counters,settings,{},second,secondError);});a.join();b.join();
    check(firstStatus==0&&secondStatus==0,"independent callers complete on shared worker pool");
    if(counters.peak>3||counters.peak<=1)std::cerr<<"Shared pool peak="<<counters.peak<<", first participants="<<first.threadsUsed<<", second participants="<<second.threadsUsed<<'\n';
    check(counters.peak<=3&&counters.peak>1,"simultaneous callers share one CPU budget rather than multiplying it");
    struct Nested {ParallelSettings settings;ParallelStatistics statistics;std::atomic_uint64_t visits{0};} nested;nested.settings=settings;
    const auto outer=[](void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity){auto& value=*static_cast<Nested*>(raw);
        const auto inner=[](void* context,uint64_t first,uint64_t last,char*,uint64_t){static_cast<Nested*>(context)->visits.fetch_add(last-first);return 0;};
        std::string message;const auto status=ThreadBudget::instance().run(end-begin,1,inner,&value,value.settings,{},value.statistics,message);
        if(status&&capacity){const auto length=std::min<std::size_t>(message.size(),capacity-1);std::copy_n(message.data(),length,error);error[length]=0;}return status;};
    std::string nestedError;const auto nestedStatus=ThreadBudget::instance().run(4096,1,outer,&nested,settings,{},nested.statistics,nestedError);
    check(nestedStatus==0&&nested.visits==4096,"nested numerical kernels execute inline without deadlock or new worker pools");
    auto cancellation=std::make_shared<std::atomic_bool>(false);std::thread canceller([cancellation]{std::this_thread::sleep_for(std::chrono::milliseconds(5));cancellation->store(true);});
    ParallelStatistics cancelledStats;std::string cancelledError;const auto cancelledStatus=ThreadBudget::instance().run(20000,1,callback,&counters,settings,cancellation,cancelledStats,cancelledError);canceller.join();
    check(cancelledStatus==PENTA_NUMERIC_CANCELLED,"active worker tasks observe cancellation");
    const auto recoveredStatus=ThreadBudget::instance().run(1024,1,callback,&counters,settings,{},cancelledStats,cancelledError);
    check(recoveredStatus==0,"worker pool remains usable after cancellation");
}
#ifndef PENTA_PARALLEL_KERNEL_ONLY
void language(){
    const auto result=execute("threads(4);integer requested=threads();assert(requested==4);vector x=linspace(0,1,20000);vector y=sin(x);vector z=axpy(2,x,y);record info=parallel_info();assert(info.requested==4);assert(info.parallel_calls>0);assert(info.module_loaded==1);assert(z(1)==0);display(z(end));threads(0);assert(threads()==0);");
    if(!result.ok()){for(const auto& error:result.analysis.diagnostics)std::cerr<<error.code<<": "<<error.message<<'\n';for(const auto& error:result.runtimeDiagnostics)std::cerr<<error.code<<": "<<error.message<<'\n';}
    check(result.ok(),"language vector operations route to the lazy parallel backend");
    auto invalid=execute("threads(-1);");check(!invalid.ok(),"negative thread count rejected");invalid=execute("threads(65);");check(!invalid.ok(),"excessive thread count rejected");
    InterpreterSession session;check(session.submit("threads(2);").ok(),"session thread count set");const auto query=session.submit("display(threads());");check(query.ok()&&query.output==std::vector<std::string>{"2"},"thread setting persists across submissions");session.reset();const auto reset=session.submit("display(threads());");check(reset.ok()&&reset.output==std::vector<std::string>{"0"},"reset restores automatic threading");
}
#endif
}
int main(){vectorTests();reductions();loaderFailures();sharedBudget();
#ifndef PENTA_PARALLEL_KERNEL_ONLY
language();
#endif
std::cout<<checks<<" parallel checks, "<<failures<<" failures\n";return failures?1:0;}
