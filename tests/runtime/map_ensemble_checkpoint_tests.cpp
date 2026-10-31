#include "maps/ensemble.hpp"
#include "runtime/data_io.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace penta;
namespace {
int checks=0;
void check(bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){try{action();}catch(const std::exception&){check(true,message);return;}throw std::runtime_error(message);}
language::FunctionDefinition function(const std::string& source){const auto parsed=language::analyse(source);if(!parsed.ok())throw std::runtime_error("Map fixture failed source checking.");return *parsed.program.statements.front().function;}
}
int main(){
    const auto directory=std::filesystem::temp_directory_path()/("penta-map-ensemble-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        native_functions::Options native;native.build.cacheDirectory=directory/"callbacks";std::atomic_size_t charged{0};native.chargeOperations=[&](std::size_t n){charged.fetch_add(n);};
        native_functions::Callback callback(function("native function real step(real x,real noise,real rho){return rho*x+noise;}"),native);
        maps::ensemble::Options options;options.callback=&callback;options.runs=8;options.coordinates=1;options.initial={0,1,2,3,4,5,6,7};options.fixedInput={0,0,.8};options.iterations=24;options.randomCoordinate=1;options.seed=729;options.threads=1;options.batchSize=8;
        const auto serial=maps::ensemble::run(options);check(serial.completed==8,"Serial stochastic ensemble completes every run");options.threads=4;const auto parallel=maps::ensemble::run(options);check(parallel.states==serial.states,"Independent task RNG streams are bitwise invariant to worker scheduling");check(parallel.parallel.parallelCalls>=1&&parallel.parallel.threadsUsed>=1&&parallel.parallel.threadsUsed<=4,"Map ensemble shares Studio's CPU worker budget");check(serial.states[0]!=serial.states[1],"Different run identities receive independent stochastic streams");
        options.seed=730;check(maps::ensemble::run(options).states!=serial.states,"A changed seed changes stochastic realizations without rebuilding the native callback");options.seed=729;
        options.checkpoint=directory/"maps.json";options.batchSize=2;const auto owner=std::this_thread::get_id();options.progress=[&](std::size_t completed,std::size_t){check(std::this_thread::get_id()==owner,"Incremental progress is delivered on the initiating thread");return completed>=2;};const auto paused=maps::ensemble::run(options);check(paused.userStopped&&paused.completed==2&&paused.checkpointOkay,"Progress callback cleanly pauses after a durable completed batch");
        options.progress={};options.resume=true;options.batchSize=3;const auto resumed=maps::ensemble::run(options);check(resumed.restored==2&&resumed.completed==8,"Resume restores completed runs and finishes only pending tasks");check(resumed.states==serial.states,"Resumed stochastic ensemble reproduces the uninterrupted realization");const auto work=charged.load();const auto completed=maps::ensemble::run(options);check(completed.restored==8&&charged.load()==work,"A completed ensemble resumes without native map evaluations");
        const auto bytes=runtime::dataio::readBytes(options.checkpoint);options.iterations=25;rejects([&]{(void)maps::ensemble::run(options);},"Changed iteration settings reject incompatible map checkpoints");check(runtime::dataio::readBytes(options.checkpoint)==bytes,"Incompatible resume leaves existing evidence unchanged");options.iterations=24;runtime::dataio::writeBytesAtomic(options.checkpoint,bytes.substr(0,bytes.size()/2));const auto corrupt=runtime::dataio::readBytes(options.checkpoint);rejects([&]{(void)maps::ensemble::run(options);},"Truncated stochastic checkpoints reject");check(runtime::dataio::readBytes(options.checkpoint)==corrupt,"Corrupt map ledger is preserved for inspection");
        options.checkpoint.clear();options.resume=false;options.distribution="normal";const auto normal=maps::ensemble::run(options);options.threads=1;check(maps::ensemble::run(options).states==normal.states,"Box-Muller normal streams preserve serial/parallel reproducibility");
        options.randomCoordinate=std::numeric_limits<std::size_t>::max();options.fixedInput={0,0,2};options.initial.assign(8,1);options.escapeRadius=4;const auto escaped=maps::ensemble::run(options);check(escaped.status[0]==2&&escaped.iterations[0]==3&&escaped.states[0]==8,"Escape status and exact stop iteration are retained per run");
        native_functions::Callback failure(function("native function real step(real x){return sqrt(x);}"),native);options.callback=&failure;options.fixedInput={0};options.runs=2;options.initial={-1,1};options.escapeRadius=0;options.iterations=2;const auto failed=maps::ensemble::run(options);check(failed.status[0]==3&&failed.status[1]==1&&!failed.messages[0].empty(),"Native domain failures are isolated from independent valid runs");
    }catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);std::cout<<checks<<" stochastic map checkpoint/progress checks passed.\n";
}
