#include "pde/pde_module.hpp"
#include "runtime/thread_budget.hpp"
#ifndef PENTA_PDE_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
int checks=0,failures=0;
void require(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
struct Reaction {
    std::size_t components=1;
    std::atomic_uint64_t calls{0},active{0},peak{0};
    bool fail=false,expensive=true;
    std::shared_ptr<std::atomic_bool> cancellation;
    uint64_t cancelAt=0;
    static int evaluate(void* raw,double t,double x,const double* u,double* out,char* error,uint64_t capacity){
        auto& self=*static_cast<Reaction*>(raw);
        const auto call=self.calls.fetch_add(1,std::memory_order_relaxed)+1;
        const auto active=self.active.fetch_add(1,std::memory_order_relaxed)+1;
        auto maximum=self.peak.load(std::memory_order_relaxed);
        while(maximum<active&&!self.peak.compare_exchange_weak(maximum,active,std::memory_order_relaxed)){}
        struct Guard{Reaction& self;~Guard(){self.active.fetch_sub(1,std::memory_order_relaxed);}} guard{self};
        if(self.cancelAt&&call>=self.cancelAt&&self.cancellation)self.cancellation->store(true);
        if(self.fail&&x>=.5&&x<.51){
            if(error&&capacity){std::strncpy(error,"deliberate native reaction failure",capacity-1);error[capacity-1]=0;}
            return 1;
        }
        double work=.125+x*.01+t*.001;
        if(self.expensive)for(int iteration=0;iteration<128;++iteration)work=std::sin(work)+.001;
        for(std::size_t j=0;j<self.components;++j)out[j]=-.1*u[j]+.00001*work+.000001*static_cast<double>(j);
        return 0;
    }
    void reset(){calls=0;active=0;peak=0;}
};
struct Field {
    std::vector<double> x,initial,left,right;
    penta_pde_request_v1 request{};
    Field(unsigned method,unsigned boundary,std::size_t components,std::size_t nodes,Reaction& reaction){
        x.resize(nodes);initial.resize(nodes*components);left.resize(components);right.resize(components);
        for(std::size_t i=0;i<nodes;++i){
            x[i]=static_cast<double>(i)/static_cast<double>(boundary==0?nodes:nodes-1);
            for(std::size_t j=0;j<components;++j)initial[i*components+j]=.5+.01*static_cast<double>(j)+.1*std::cos(2*3.141592653589793*x[i]);
        }
        for(std::size_t j=0;j<components;++j){left[j]=initial[j];right[j]=initial[(nodes-1)*components+j];}
        reaction.components=components;
        request.struct_size=sizeof(request);request.method=method;request.boundary=boundary;request.components=static_cast<uint32_t>(components);
        request.nodes=nodes;request.x=x.data();request.initial=initial.data();request.left=left.data();request.right=right.data();
        request.length=1;request.diffusion=.01;request.t0=0;request.t1=.001;request.time_step=.0001;request.sample_step=.0005;
        request.max_wall_seconds=10;request.max_steps=100000;request.max_reaction_evaluations=10000000;
        request.reaction_context=&reaction;request.reaction=Reaction::evaluate;
    }
};
void agreementAndRealConcurrency(){
    penta::pde::Module module;auto cancellation=std::make_shared<std::atomic_bool>(false);
    for(unsigned method=0;method<2;++method)for(unsigned boundary=0;boundary<3;++boundary)for(const std::size_t components:{1u,5u}){
        Reaction reaction;Field field(method,boundary,components,256,reaction);
        std::size_t charge1=0,charge4=0;
        const auto serial=module.solve(field.request,cancellation,[&](std::size_t count){charge1+=count;},1);
        reaction.reset();const auto parallel=module.solve(field.request,cancellation,[&](std::size_t count){charge4+=count;},4);
        const auto label=std::string(method?"FE":"FD")+"/boundary"+std::to_string(boundary)+"/components"+std::to_string(components);
        require(serial.values==parallel.values,label+" exact serial/parallel field agreement");
        require(serial.means==parallel.means&&serial.rms==parallel.rms,label+" exact deterministic reductions");
        require(serial.times==parallel.times&&serial.report.steps==parallel.report.steps,label+" unchanged RK4 samples/steps");
        require(serial.report.reaction_evaluations==parallel.report.reaction_evaluations,label+" unchanged reaction evaluation count");
        require(charge1==charge4,label+" unchanged owner work charge");
        require(serial.statistics.threadsUsed==1&&serial.threadsGranted==1,label+" explicit serial policy");
        require(parallel.statistics.requestedThreads==4&&parallel.threadsGranted==4,label+" explicit thread policy reported");
        require(parallel.statistics.threadsUsed>1&&parallel.statistics.threadsUsed<=4,label+" actual pool participants");
        require(parallel.statistics.peakActiveThreads>1&&parallel.statistics.peakActiveThreads<=4,label+" simultaneous native ranges");
        require(reaction.peak.load()>1&&reaction.peak.load()<=4,label+" actual overlapping native callbacks");
        require(reaction.calls.load()==parallel.report.reaction_evaluations,label+" actual callback report count");
        require(parallel.statistics.parallelCalls>0,label+" parallel dispatch recorded");
    }
    Reaction reaction;Field field(0,0,1,256,reaction);
    const auto automatic=module.solve(field.request,cancellation,{},0);
    require(automatic.statistics.requestedThreads==0,"automatic request preserved");
    require(automatic.threadsGranted==penta::runtime::ThreadBudget::effectiveThreads(0),"automatic grant uses shared runtime policy");
    require(automatic.statistics.threadsUsed<=automatic.threadsGranted,"actual participants bounded by auto grant");
    bool invalid=false;try{(void)module.solve(field.request,cancellation,{},65);}catch(const penta::pde::Error&){invalid=true;}
    require(invalid,"malformed native thread request rejected");
}
void cancellationFailureAndReuse(){
    penta::pde::Module module;auto cancellation=std::make_shared<std::atomic_bool>(false);
    for(unsigned method=0;method<2;++method){
        Reaction reaction;reaction.cancellation=cancellation;Field field(method,0,5,256,reaction);
        reaction.fail=true;bool failed=false;
        try{(void)module.solve(field.request,cancellation,{},4);}catch(const penta::pde::Error& error){failed=std::string(error.what()).find("deliberate")!=std::string::npos;}
        require(failed,"parallel callback failure retains useful diagnostic");
        require(reaction.active.load()==0,"parallel callback failure joins every worker");
        reaction.fail=false;reaction.reset();reaction.cancelAt=300;
        bool cancelled=false;const auto begin=std::chrono::steady_clock::now();
        try{(void)module.solve(field.request,cancellation,{},4);}catch(const penta::pde::Error& error){cancelled=error.status==3;}
        require(cancelled,"parallel native solve classifies cancellation");
        require(std::chrono::steady_clock::now()-begin<std::chrono::seconds(3),"parallel cancellation returns promptly");
        require(reaction.active.load()==0,"parallel cancellation joins every worker");
        cancellation->store(false);reaction.cancelAt=0;reaction.reset();
        auto restored=module.solve(field.request,cancellation,{},4);
        require(restored.report.steps>0&&std::string(restored.report.status)=="completed","pool/module reusable after failures and cancellation");
        bool ownerBudget=false;
        try{(void)module.solve(field.request,cancellation,[](std::size_t){throw std::runtime_error("owner quota");},4);}catch(const std::exception& error){ownerBudget=std::string(error.what())=="owner quota";}
        require(ownerBudget,"owner budget exception propagates exactly");
        require(reaction.active.load()==0,"owner budget failure leaves no numerical workers running");
    }
}
void nestedBudget(){
    struct Nested{
        std::atomic_int failure{0};
        static int run(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity)noexcept{
            auto& self=*static_cast<Nested*>(raw);
            try{for(auto i=begin;i<end;++i){(void)i;Reaction reaction;Field field(1,0,5,128,reaction);auto cancel=std::make_shared<std::atomic_bool>(false);
                const auto result=penta::pde::Module().solve(field.request,cancel,{},4);
                if(result.threadsGranted!=1||result.statistics.threadsUsed!=1||result.statistics.parallelCalls!=0)self.failure=1;
            }return 0;}catch(const std::exception& e){if(error&&capacity){std::strncpy(error,e.what(),capacity-1);error[capacity-1]=0;}return 1;}
        }
    } nested;
    penta::runtime::ParallelSettings settings;settings.threads=4;settings.serialThreshold=1;
    penta::runtime::ParallelStatistics statistics;std::string detail;
    const int status=penta::runtime::ThreadBudget::instance().run(4,2048,Nested::run,&nested,settings,{},statistics,detail);
    require(status==0,"nested PDE calls finish without CPU reservation deadlock: "+detail);
    require(nested.failure==0,"nested native clients are granted one inner CPU");
    require(statistics.threadsUsed>1&&statistics.threadsUsed<=4,"outer numerical pool remains in control");
}
void oldAbiPrefix(const std::filesystem::path& directory){
    const auto path=directory/"modules"/
#ifdef _WIN32
        "penta_pde.dll";
    auto handle=LoadLibraryW(path.c_str());require(handle!=nullptr,"load module for legacy ABI test");if(!handle)return;
    const auto query=reinterpret_cast<penta_pde_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_pde_query_v1")));
#else
        "libpenta_pde.so";
    auto handle=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);require(handle!=nullptr,"load module for legacy ABI test");if(!handle)return;
    const auto query=reinterpret_cast<penta_pde_query_function_v1>(dlsym(handle,"penta_pde_query_v1"));
#endif
    const auto* api=query?query(PENTA_PDE_ABI_V1):nullptr;require(api&&api->struct_size>=sizeof(penta_pde_parallel_api_v1),"new capability is negotiated behind original API prefix");
    if(api){
        require((reinterpret_cast<const penta_pde_parallel_api_v1*>(api)->capabilities&PENTA_PDE_CAP_PARALLEL_V1)!=0,"native parallel capability advertised");
        Reaction reaction;Field field(0,0,1,64,reaction);std::vector<double> times(8),values(8*64),means(8),rms(8);
        penta_pde_host_v1 host{sizeof(host),PENTA_PDE_ABI_V1,nullptr,nullptr,nullptr};penta_pde_result_v1 result{};
        result.struct_size=sizeof(result);result.times=times.data();result.values=values.data();result.means=means.data();result.spatial_rms=rms.data();result.sample_capacity=8;result.value_capacity=values.size();
        char error[1024]{};const int status=api->solve(&host,&field.request,&result,error,sizeof(error));
        require(status==0,"legacy host/request/result prefixes remain accepted: "+std::string(error));
        require(reaction.peak.load()==1,"legacy host without dispatcher remains serial");
    }
#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}
#ifndef PENTA_PDE_KERNEL_ONLY
void languageBridge(){
    const auto temporary=std::filesystem::temp_directory_path()/("penta-pde-parallel-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary);
    penta::runtime::ExecutionOptions options;options.dataDirectory=temporary;options.odeCacheDirectory=temporary/"cache";
    auto run=[&](const std::string& source,bool expected=true){
        const auto result=penta::runtime::execute(source,{},options);
        require(result.ok()==expected,"language parallel PDE expected status");
        if(result.ok()!=expected){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
        return result;
    };
    const std::string declaration="native function pentarnion reaction_parallel(pentarnion p,real t,real x,pentarnion A,pentarnion B,real eta){return A*p+(1-eta)*(B*(p*p))+eta*((B*p)*p);}";
    const std::string setup=declaration+"matrix x=(0:255)/256;matrix p=1+i-j+.2*k+.3*ell+.001*cos(x);pentarnion A=-.5-.2*i;pentarnion B=.1+.2*j;";
    const std::string settings="parameters=options(A=A,B=B,eta=0.0),length=1,diffusion=.01,t1=.002,time_step=.0001,sample_step=.001";
    for(const std::string method:{"fd","fem"}){
        run(setup+"threads(3);record a=pdesolve(reaction_parallel,x,p,options(method=\""+method+"\","+settings+",threads=1));record b=pdesolve(reaction_parallel,x,p,options(method=\""+method+"\","+settings+",threads=4));"
            "for(row=1:size(a.values,1)){for(column=1:size(a.values,2)){assert(norm2(a.values(row,column)-b.values(row,column))==0);}}"
            "for(row=1:size(a.mean,1)){assert(norm2(a.mean(row)-b.mean(row))==0);}assert(norm(a.spatial_rms-b.spatial_rms)==0);"
            "assert(a.native_operations==b.native_operations);assert(a.reaction_evaluations==b.reaction_evaluations);assert(a.cache_key==b.cache_key&&b.cache_hit);"
            "assert(a.threads_used==1&&a.threads_requested==1);assert(b.threads_used>1&&b.threads_used<=4);assert(b.threads_granted==4&&b.peak_active_threads<=4&&b.parallel_calls>0);assert(threads()==3);"
            "record inherited=pdesolve(reaction_parallel,x,p,options(method=\""+method+"\","+settings+"));assert(inherited.threads_requested==3);"
            "record automatic=pdesolve(reaction_parallel,x,p,options(method=\""+method+"\","+settings+",threads=0));assert(automatic.threads_requested==0&&automatic.threads_used<=automatic.threads_granted);assert(threads()==3);");
    }
    const std::string small="native function real finite_reaction(real p,real t,real x){return p*p+sin(t+x);}matrix x=(0:255)/256;";
    for(const auto threads:{1,4}){
        const auto failed=run(small+"record field=pdesolve(finite_reaction,x,ones(1,256),options(length=1,diffusion=0,t1=.1,threads="+std::to_string(threads)+",max_native_operations=2000));",false);
        require(std::any_of(failed.runtimeDiagnostics.begin(),failed.runtimeDiagnostics.end(),[](const auto& d){return d.code=="R1005";}),"atomic native budget exhaustion stays R1005");
    }
    for(const std::string threads:{"-1","65","1.5","[2]"})run(small+"record field=pdesolve(finite_reaction,x,ones(1,256),options(length=1,diffusion=0,t1=.001,threads="+threads+"));",false);
    run(small+"record field=pdesolve(finite_reaction,x,ones(1,256),options(length=1,diffusion=0,t1=.001,threads=4));assert(field.threads_used>1);");
    // Loaded generated DLLs remain resident until process exit on Windows.
    std::error_code ignored;std::filesystem::remove_all(temporary,ignored);
}
#endif
}
int main(int argc,char** argv){
    try{
        agreementAndRealConcurrency();cancellationFailureAndReuse();nestedBudget();
        if(argc>0)oldAbiPrefix(std::filesystem::absolute(argv[0]).parent_path());
#ifndef PENTA_PDE_KERNEL_ONLY
        languageBridge();
#endif
    }catch(const std::exception& failure){require(false,std::string("unexpected exception: ")+failure.what());}
    std::cout<<checks<<" PDE parallel checks, "<<failures<<" failures\n";return failures?1:0;
}
