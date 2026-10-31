#include "pde/pde_module.hpp"
#include "runtime/interpreter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
int checks=0,failures=0;
void require(bool condition,const std::string& message){
    ++checks;
    if(!condition){++failures;std::cerr<<"FAIL: "<<message<<'\n';}
}
void diagnostics(const penta::runtime::ExecutionResult& result){
    for(const auto& item:result.analysis.diagnostics)std::cerr<<item.code<<": "<<item.message<<'\n';
    for(const auto& item:result.runtimeDiagnostics)std::cerr<<item.code<<": "<<item.message<<'\n';
}
struct Reaction {
    std::shared_ptr<std::atomic_bool> cancellation;
    std::size_t calls=0;
    bool cancelFirst=false;
    static int evaluate(void* context,double,double,const double*,double* result,char*,std::uint64_t){
        auto& self=*static_cast<Reaction*>(context);
        ++self.calls;
        result[0]=0;
        if(self.cancelFirst&&self.calls==1)self.cancellation->store(true);
        return 0;
    }
};

void directCancellation(unsigned method){
    const std::size_t nodes=64;
    std::vector<double> x(nodes),initial(nodes,1);
    for(std::size_t i=0;i<nodes;++i)x[i]=static_cast<double>(i)/static_cast<double>(nodes);
    auto cancellation=std::make_shared<std::atomic_bool>(true);
    Reaction reaction{cancellation};
    penta_pde_request_v1 request{};
    request.struct_size=sizeof(request);request.method=method;request.boundary=0;
    request.components=1;request.nodes=nodes;request.x=x.data();request.initial=initial.data();
    request.length=1;request.diffusion=.01;request.t0=0;request.t1=1;
    request.time_step=.0001;request.sample_step=.1;request.max_wall_seconds=5;
    request.max_steps=100000;request.max_reaction_evaluations=10000000;
    request.reaction_context=&reaction;request.reaction=Reaction::evaluate;
    penta::pde::Module module;
    bool cancelled=false;
    auto start=Clock::now();
    try{(void)module.solve(request,cancellation,{});}catch(const penta::pde::Error& error){cancelled=error.status==3;}
    require(cancelled,"pre-cancelled native PDE must report cancellation");
    require(reaction.calls==0,"pre-cancelled native PDE must not evaluate reaction");
    require(Clock::now()-start<2s,"pre-cancelled native PDE response must be bounded");

    cancellation->store(false);reaction.cancelFirst=true;cancelled=false;start=Clock::now();
    try{(void)module.solve(request,cancellation,{});}catch(const penta::pde::Error& error){cancelled=error.status==3;}
    require(cancelled,"cancellation from a native reaction must stop before next RK4 stage");
    require(reaction.calls==nodes,"generic host callback cancellation must stop within current RHS pass");
    require(Clock::now()-start<2s,"mid-RHS native PDE response must be bounded");

    cancellation->store(false);reaction.cancelFirst=false;reaction.calls=0;
    request.t1=.0002;request.sample_step=.0001;
    const auto result=module.solve(request,cancellation,{});
    require(result.report.steps==2,"module remains usable after cancellation");
    require(std::all_of(result.values.begin(),result.values.end(),[](double value){return std::abs(value-1)<1e-10;}),"reuse preserves constant solution");
}

const std::string declaration=R"PENTA(
native function real relaxation_cancel_test(real u,real t,real s){
    return sin(u)*cos(u)-u+0.001*cos(t+s);
}
)PENTA";
std::string script(const std::string& method,const std::string& finalTime){
    return declaration+"matrix x=(0:127)/128;matrix initial=ones(1,128);"
        "record field=pdesolve(relaxation_cancel_test,x,initial,options(method=\""+method+
        "\",length=1,diffusion=0.01,t1="+finalTime+
        ",time_step=.0001,sample_step=1,max_wall_seconds=30));";
}
}

int main(){
    directCancellation(0);directCancellation(1);
    penta::runtime::ExecutionOptions options;
    options.dataDirectory=std::filesystem::temp_directory_path()/("penta-pde-cancel-"+std::to_string(Clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(options.dataDirectory);
    options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";
    auto warm=penta::runtime::execute(script("fd",".0002"),{},options);
    require(warm.ok(),"warm callback before timed cancellation");if(!warm.ok())diagnostics(warm);
    for(const std::string method:{"fd","fem"}){
        for(int attempt=0;attempt<8;++attempt){
            options.cancellation=std::make_shared<std::atomic_bool>(false);
            const auto cancellation=options.cancellation;
            penta::runtime::InterpreterSession session;
            const auto seeded=session.submit("real retained=17;",{},options);
            require(seeded.ok(),"seed previous command workspace");
            const auto start=Clock::now();
            std::jthread trigger([cancellation]{std::this_thread::sleep_for(20ms);cancellation->store(true);});
            const auto result=session.runFile(script(method,"100"),{},options);
            trigger.join();
            const bool stopped=result.status==penta::runtime::ExecutionStatus::Cancelled;
            require(stopped,method+" warm long solve cancellation reports Cancelled, not RuntimeError");
            if(!stopped)diagnostics(result);
            require(Clock::now()-start<3s,method+" warm long solve responds promptly");
            require(result.figureActions.empty(),"cancelled PDE must not commit pending plots");
            require(std::any_of(result.variables.begin(),result.variables.end(),[](const auto& variable){return variable.name=="retained"&&variable.value=="17";}),"cancelled Run File restores pre-run workspace");
            require(std::none_of(result.runtimeDiagnostics.begin(),result.runtimeDiagnostics.end(),[](const auto& item){return item.code=="R2100";}),"cancelled PDE must not surface a PDE-domain diagnostic");
            cancellation->store(false);
            const auto reused=session.submit("assert(retained==17);real after_cancel=23;",{},options);
            require(reused.ok(),"command workspace is usable after PDE cancellation");
            if(!reused.ok())diagnostics(reused);
        }
    }
    // A fresh body/cache cancellation also uses the native compiler's bounded
    // job cancellation path; existing ODE cache tests exercise child termination.
    options.cancellation=std::make_shared<std::atomic_bool>(false);
    options.odeCacheDirectory=options.dataDirectory/"cold-cache";
    const auto cancellation=options.cancellation;
    const auto start=Clock::now();
    std::jthread trigger([cancellation]{std::this_thread::sleep_for(20ms);cancellation->store(true);});
    const auto cold=penta::runtime::execute(script("fd","100"),{},options);
    trigger.join();
    require(cold.status==penta::runtime::ExecutionStatus::Cancelled,"cold native compile cancellation reports Cancelled");
    require(Clock::now()-start<5s,"cold compile cancellation terminates bounded compiler work");
    if(cold.status!=penta::runtime::ExecutionStatus::Cancelled)diagnostics(cold);
    options.cancellation->store(false);
    const auto recovered=penta::runtime::execute(script("fd",".0002"),{},options);
    require(recovered.ok(),"interrupted cold cache can be built and reused");if(!recovered.ok())diagnostics(recovered);
    // Non-cancellation PDE errors keep their existing classification.
    const auto bad=penta::runtime::execute(declaration+"record bad=pdesolve(relaxation_cancel_test,[0,.2,.5],[1,1,1],options(method=\"fd\",length=1));",{},options);
    require(bad.status==penta::runtime::ExecutionStatus::RuntimeError,"invalid PDE remains a runtime error");
    require(std::any_of(bad.runtimeDiagnostics.begin(),bad.runtimeDiagnostics.end(),[](const auto& item){return item.code=="R2100";}),"invalid PDE preserves R2100");
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
    std::cout<<checks<<" PDE cancellation checks, "<<failures<<" failures\n";
    return failures?1:0;
}
