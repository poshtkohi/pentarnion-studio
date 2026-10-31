#include "runtime/interpreter.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {
int checks=0,failures=0;void expect(bool value,const std::string& label){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
bool hasCode(const penta::runtime::ExecutionResult& result,const std::string& code){for(const auto& d:result.runtimeDiagnostics)if(d.code==code)return true;return false;}
bool hasMessage(const penta::runtime::ExecutionResult& result,const std::string& value){for(const auto& d:result.runtimeDiagnostics)if(d.message.find(value)!=std::string::npos)return true;return false;}
const std::string objective=R"(
native function real quota_bowl(real x,real y){return (x-0.25)^2+(y+0.5)^2;}
)";
std::string solve(unsigned threads,const std::string& extra="",unsigned population=32,unsigned generations=8){return "record r=evolve(quota_bowl,[0.7,-0.8],options(algorithm=\"cmaes\",lower=-1.0,upper=1.0,restarts=0,population="+std::to_string(population)+",max_generations="+std::to_string(generations)+",max_evaluations="+std::to_string(population*(generations+1))+",ftol=0.0,xtol=0.0,seed=42,threads="+std::to_string(threads)+extra+"));display(r.x);display(r.evaluations);";}
const std::string passive=R"(
function integer passive_quota_monitor(matrix x,record info=iterationinfo(),string state="iter"){
    assert(info.dimension==numel(x));return 0;
}
)";
const std::string costly=R"(
function integer costly_quota_monitor(matrix x,record info=iterationinfo(),string state="iter"){
    integer repeats=0;real total=0.0;
    while(repeats<40){total=total+sin(repeats*0.01);repeats=repeats+1;}
    assert(total>=0);figure(31);plot([0,1],[info.funccount,info.funccount]);
    return 0;
}
)";
}
int main(){using namespace penta::runtime;ExecutionOptions options;options.maxOperations=10000000;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-evolution-quota-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";
    auto run=[&](const std::string& source,const ExecutionOptions& configured,bool good=true){const auto r=execute(source,{},configured);if(r.ok()!=good){for(const auto& d:r.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:r.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(r.ok()==good,"evolution operation quota script status");expect(r.operations<=configured.maxOperations,"owner operation counter stays within limit");return r;};
    const auto serial=run(objective+solve(1),options),parallel=run(objective+solve(4),options);expect(serial.output==parallel.output,"atomic quota preserves seeded serial/parallel candidate and evaluation counts");expect(serial.operations==parallel.operations,"owner-committed work is identical across tested thread counts");
    const auto observedSerial=run(objective+passive+solve(1,",OutputFcn=passive_quota_monitor"),options),observedParallel=run(objective+passive+solve(4,",OutputFcn=passive_quota_monitor"),options);expect(observedSerial.output==observedParallel.output&&observedSerial.output==serial.output,"passive observer and parallel quota add no evaluations or candidate changes");expect(observedSerial.operations==observedParallel.operations,"observer epochs commit the same serial/parallel work");expect(observedSerial.operations>serial.operations,"observer work is included in the owner's operation total");
    for(unsigned threads:{1u,4u}){auto limited=options;limited.maxOperations=1000;const auto rejected=run(objective+solve(threads,"",1024,2),limited,false);expect(hasCode(rejected,"R1005"),"native/module quota exhaustion reports owner R1005 rather than callback/internal error");expect(!hasCode(rejected,"R2040"),"quota exhaustion wins before native bridge failure conversion");}
    std::size_t frames=0;auto withFrames=options;withFrames.figureProgress=[&](const std::vector<FigureAction>&){++frames;};const auto costlyFull=run(objective+costly+solve(4,",OutputFcn=costly_quota_monitor"),withFrames);expect(frames>=10,"costly observer runs init/iterations/done on owner");expect(costlyFull.operations>observedParallel.operations,"callback computations and plots consume shared work quota");
    auto limitedObserver=withFrames;limitedObserver.maxOperations=observedParallel.operations+(costlyFull.operations-observedParallel.operations)/2;frames=0;const auto observerBudget=run(objective+costly+solve(4,",OutputFcn=costly_quota_monitor"),limitedObserver,false);expect(hasCode(observerBudget,"R1005"),"quota refresh accounts for observer-consumed operations");expect(frames>0&&frames<10,"exhausted observer quota stops before every generation/DONE is published");
    const std::string broken=R"(
function integer broken_quota_monitor(matrix x,record info=iterationinfo(),string state="iter"){
    if(state=="iter"&&info.iteration>=2){error("intentional quota observer failure");}return 0;
}
)";
    const auto observerError=run(objective+broken+solve(4,",OutputFcn=broken_quota_monitor"),options,false);expect(hasMessage(observerError,"intentional quota observer failure"),"original owner observer error survives module unwind");expect(observerError.operations>500,"completed parallel native work is committed before observer failure");
    const std::string invalid=R"(native function real domain_quota(real x,real y){return sqrt(x-10.0)+y;}record r=evolve(domain_quota,[0.0,0.0],options(algorithm="cmaes",lower=-1.0,upper=1.0,population=32,threads=4,restarts=0,max_generations=2));)";const auto domain=run(invalid,options,false);expect(hasCode(domain,"R2040")&&!hasCode(domain,"R1005"),"a genuine native domain error stays distinct from operation exhaustion");
    auto cancellation=std::make_shared<std::atomic_bool>(false);auto cancelOptions=options;cancelOptions.cancellation=cancellation;std::size_t cancelFrames=0;const auto driver=std::this_thread::get_id();bool wrongThread=false;cancelOptions.figureProgress=[&](const std::vector<FigureAction>&){wrongThread|=std::this_thread::get_id()!=driver;if(++cancelFrames==3)cancellation->store(true);};const auto cancelled=run(objective+costly+solve(4,",OutputFcn=costly_quota_monitor",64,30),cancelOptions,false);expect(cancelFrames==3&&!wrongThread,"observer cancellation occurs on owner after joined parallel generations");expect(hasCode(cancelled,"R1006")&&!hasCode(cancelled,"R1005"),"cancellation keeps its distinct owner diagnostic");expect(cancelled.operations>500,"parallel work committed before cancellation unwind");
    cancellation->store(false);const auto reused=run(objective+solve(4),options);expect(reused.output==parallel.output,"callback cache/shared worker pool remain reusable after budget/domain/observer/cancel errors");
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" evolutionary quota checks, "<<failures<<" failures\n";return failures?1:0;
}
