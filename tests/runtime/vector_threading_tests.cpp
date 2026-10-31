#include "runtime/interpreter.hpp"
#include "runtime/numeric_module_loader.hpp"
#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#ifndef PENTA_SOURCE_DIR
#define PENTA_SOURCE_DIR "."
#endif
using namespace penta;
namespace {
int checks=0,failures=0;
void check(bool value,const std::string& label){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
void diagnostics(const runtime::ExecutionResult& result){if(result.ok())return;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
}
int main(){
    runtime::ExecutionOptions execution;execution.maxOperations=4000000;
    auto valid=[&](const std::string& source,const std::string& label){const auto result=runtime::execute(source,{},execution);diagnostics(result);check(result.ok(),label);};
    auto invalid=[&](const std::string& source,const std::string& label){check(!runtime::execute(source,{},execution).ok(),label);};
    try{
        valid("threads(4);matrix x=linspace(0,1,40000);matrix a=sin(x,options(threads=1));record one=parallel_info();assert(threads()==4);assert(one.requested==4);assert(one.last_requested==1);assert(one.threads_used==1);matrix b=sin(x,options(threads=4));record four=parallel_info();assert(four.last_requested==4);assert(four.parallel_calls>one.parallel_calls);assert(four.threads_used>=1&&four.threads_used<=4);assert(a==b);", "per-call scalar/vector module worker limits preserve session default");
        valid("threads(3);matrix x=linspace(0,1,40000);matrix y=expm1(x,options(threads=1));record one=parallel_info();assert(one.last_requested==1);matrix z=log1p(y,options(threads=4));record four=parallel_info();assert(four.last_requested==4);assert(four.parallel_calls>one.parallel_calls);assert(four.threads_used>=1&&four.threads_used<=4);assert(abs(z(end)-1.0)<1e-14);matrix e=erf([0.0,1.0],options(threads=1));matrix c=erfc([0.0,1.0],options(threads=4));matrix s=sign([-2.0,0.0,3.0],options(threads=2));assert(abs(e(2)+c(2)-1.0)<1e-15);assert(s==[-1.0,0.0,1.0]);assert(threads()==3);", "existing additional real math executes through per-call native kernels");
        valid("threads(4);matrix x=linspace(0,1,40000);matrix y=axpy(2.0,x,x,options(threads=1));record a=parallel_info();assert(a.last_requested==1);assert(a.threads_used==1);real d=dot(x,y,options(threads=2));record b=parallel_info();assert(b.last_requested==2);assert(b.threads_used<=2);real n=norm(y,options(threads=1));record c=parallel_info();assert(c.last_requested==1);assert(n>0&&d>0);matrix total=sum(x,options(threads=2));record r=parallel_info();assert(r.last_requested==2);assert(abs(total(1)-20000.0)<1e-8);assert(threads()==4);", "dot/norm/AXPY/reduction per-call forwarding");
        valid("threads(3);matrix A=reshape(1:40000,200,200,options(threads=1));matrix T=transpose(A,options(threads=2));assert(T(2,1)==A(1,2));matrix s=sum(A,2,options(threads=1));matrix m=mean(A,1,options(threads=2));assert(s(1)>0&&m(1)>0);matrix lo=min(A,[],2,options(threads=1));matrix hi=max(A,[],2,options(threads=2));assert(lo(1)<=hi(1));assert(threads()==3);", "matrix reductions and transpose inherit scoped per-call bounds");
        valid("threads(4);[matrix X,matrix Y]=meshgrid([0.0,1.0,2.0],[0.0,1.0,2.0],options(threads=1));matrix F=X.^2+Y.^2;[matrix gx,matrix gy]=gradient(F,options(threads=2));matrix L=laplacian(F,options(threads=1));assert(gx(2,2)==2.0&&gy(2,2)==2.0);assert(L(2,2)==4.0);assert(threads()==4);", "multi-output shape schema and sampled calculus options forward correctly");
        valid("threads(2);matrix p=[1.0+i,1.0+j];matrix q=sin(p,options(threads=1));assert(q(1)==sin(p(1)));matrix pure_part=pure(p,options(threads=4));assert(scalar(pure_part(1))==0.0);assert(threads()==2);", "ordered whole-Pentarnion math retains per-call settings");
        valid("threads(2);matrix x=[1.0,2.0];matrix y=sin(x,options(threads=0));assert(threads()==2);record r=parallel_info();assert(r.last_requested==0);assert(r.last_effective>=1);", "explicit per-call automatic selection remains distinct from session default");
        invalid("matrix y=sin([1.0,2.0],options(thread=1));", "unknown per-call numerical setting rejected");
        invalid("matrix y=sin([1.0,2.0],options(threads=1.0));", "thread option requires Integer type");
        invalid("matrix y=sin([1.0,2.0],options(threads=65));", "per-call thread maximum checked");
        invalid("matrix y=sin([1.0,2.0],options(threads=1),options(threads=2));", "duplicate trailing options records rejected");
        runtime::InterpreterSession session;check(session.submit("threads(4);").ok(),"session thread default fixture");const auto failed=session.submit("matrix y=asin([2.0],options(threads=1));");check(!failed.ok(),"native arithmetic failure is reported");const auto restored=session.submit("threads()");check(restored.ok()&&restored.output==std::vector<std::string>{"4"},"error restores per-call thread override");
        auto cancelled=execution;cancelled.cancellation=std::make_shared<std::atomic_bool>(true);const auto stopped=session.submit("matrix y=sin([1.0,2.0],options(threads=1));",{},cancelled);check(stopped.status==runtime::ExecutionStatus::Cancelled,"cancelled per-call operation remains cancelled");const auto after=session.submit("threads()");check(after.ok()&&after.output==std::vector<std::string>{"4"},"cancellation preserves session thread default");
        // A short numerical call can finish on its owner before another worker
        // is scheduled. Check dispatch above; use synchronized tasks to prove
        // actual concurrency without relying on timing or a machine's speed.
        struct Overlap { std::barrier<> arrived{4}; std::atomic_size_t participants{0}; } overlap;
        const auto coordinated=[](void* raw,uint64_t begin,uint64_t end,char*,uint64_t){auto& state=*static_cast<Overlap*>(raw);for(auto i=begin;i<end;++i){(void)i;++state.participants;state.arrived.arrive_and_wait();}return 0;};
        runtime::ParallelSettings coordinatedSettings;coordinatedSettings.threads=4;coordinatedSettings.serialThreshold=1;runtime::ParallelStatistics coordinatedStats;std::string coordinatedError;
        check(runtime::ThreadBudget::instance().run(4,2048,coordinated,&overlap,coordinatedSettings,{},coordinatedStats,coordinatedError)==0&&overlap.participants==4&&coordinatedStats.threadsUsed==4&&coordinatedStats.peakActiveThreads==4,"coordinated native tasks exercise all four granted slots concurrently");
        struct Nested {runtime::NumericModule module;std::atomic_size_t calls{0};std::atomic_bool okay{true};runtime::ParallelStatistics* outerStatistics=nullptr;} nested;
        const auto worker=[](void* raw,uint64_t begin,uint64_t end,char*,uint64_t){auto& state=*static_cast<Nested*>(raw);for(auto i=begin;i<end;++i){(void)i;double input[3]{1,2,3},output[3]{};penta_numeric_vector_request_v1 request{};request.struct_size=sizeof(request);request.operation=PENTA_VEC_SIN;request.count=request.a_count=3;request.a_width=request.output_width=1;request.a=input;request.output=output;request.output_capacity=3;runtime::ParallelSettings settings;settings.threads=8;runtime::ParallelStatistics statistics;try{if(runtime::ThreadBudget::backendThreadLimit(8)!=1)state.okay=false;state.module.vector(request,settings,{},statistics);if(statistics.threadsUsed>1||settings.threads!=8)state.okay=false;double sum=0;penta_numeric_reduce_request_v1 reduction{};reduction.struct_size=sizeof(reduction);reduction.operation=PENTA_REDUCE_SUM;reduction.width=1;reduction.count=3;reduction.a=input;reduction.output=&sum;reduction.output_capacity=1;const auto before=state.outerStatistics->lastElements;state.module.reduce(reduction,settings,{},*state.outerStatistics);if(sum!=6||state.outerStatistics->lastElements!=before)state.okay=false;}catch(...){state.okay=false;}++state.calls;}return 0;};
        runtime::ParallelSettings outer;outer.threads=4;outer.serialThreshold=1;runtime::ParallelStatistics statistics;nested.outerStatistics=&statistics;std::string error;const auto nestedStatus=runtime::ThreadBudget::instance().run(16,2048,worker,&nested,outer,{},statistics,error);check(nestedStatus==0&&nested.calls==16&&nested.okay,"nested native calls receive one CPU slot without oversubscription/deadlock");check(statistics.threadsUsed<=4&&statistics.peakActiveThreads<=4&&statistics.lastElements==16,"nested reductions do not mutate shared outer-dispatch statistics");
        const auto example=std::filesystem::path(PENTA_SOURCE_DIR)/"examples"/"parallel-vectors"/"per_call_threads.p";std::ifstream stream(example,std::ios::binary);const std::string source{std::istreambuf_iterator<char>(stream),{}};check(!source.empty(),"per-call example source exists");valid(source,"original per-call threading example");
    }catch(const std::exception&e){++failures;std::cerr<<e.what()<<'\n';}
    std::cout<<checks<<" vector threading checks, "<<failures<<" failures\n";return failures?1:0;
}
