#include "discovery/verification.hpp"
#include "runtime/data_io.hpp"
#include <chrono>
#include <cmath>
#include <iostream>

using namespace penta;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F action,const char* message){try{action();}catch(const std::exception&){check(true,message);return;}throw std::runtime_error(message);}
}
int main(int argc,char** argv){
    const auto directory=std::filesystem::temp_directory_path()/("penta-verification-checkpoint-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        const auto analysis=language::analyse("ode decay(t,real x){x'=-x;}");check(analysis.ok(),"Fixture parses");const auto& model=*analysis.program.statements.front().ode;
        ode::NativeOptions native;native.cacheDirectory=directory/"kernels";
        discovery::Options input;input.runs=1;input.physicalCoordinates=1;input.initial={1};input.gamma={1};input.threads=1;input.solver.t1=.01;input.solver.sampleStep=.01;input.solver.rtol=1e-9;input.solver.atol=1e-11;
        discovery::verification::Options options;options.runIds={17};options.horizons={.02,.04,.08};options.tightening=1;options.checkpoint=directory/"tiers.json";options.tierCompleted=[](std::size_t tier,std::size_t){if(tier==1)throw std::runtime_error("test-interruption-after-tier");};
        const auto modules=argc>1?std::filesystem::absolute(argv[1]):std::filesystem::path{};
        rejected([&]{(void)discovery::verification::run(model,input,options,native,modules);},"An interruption after a completed tier propagates");check(std::filesystem::exists(options.checkpoint),"Completed tier is durably checkpointed before interruption");const auto previous=runtime::dataio::readBytes(options.checkpoint);
        options.resume=true;std::vector<std::size_t> progress;options.tierCompleted=[&](std::size_t tier,std::size_t){progress.push_back(tier);};input.threads=2;
        const auto resumed=discovery::verification::run(model,input,options,native,modules);check(resumed.restoredTiers==1&&resumed.completed==3&&resumed.rows.size()==3,"Resume restores the complete tier prefix");check(progress==std::vector<std::size_t>{2,3},"Only unfinished tiers execute after resume with a changed worker count");check(std::abs(resumed.rows.back().state[0]-std::exp(-.08))<1e-8,"Resumed final tier matches the independent analytic solution");check(resumed.checkpointOkay,"Every completed tier updates the atomic ledger");
        options.tierCompleted={};const auto complete=discovery::verification::run(model,input,options,native,modules);check(complete.restoredTiers==3&&complete.rows.size()==3,"A fully completed verification resumes without numerical reruns");
        auto wrong=input;wrong.initial={2};const auto completeBytes=runtime::dataio::readBytes(options.checkpoint);rejected([&]{(void)discovery::verification::run(model,wrong,options,native,modules);},"Changed scientific inputs reject verification resume");check(runtime::dataio::readBytes(options.checkpoint)==completeBytes,"Incompatible resume preserves previous checkpoint bytes");
        runtime::dataio::writeBytesAtomic(options.checkpoint,previous.substr(0,previous.size()/2));const auto corrupt=runtime::dataio::readBytes(options.checkpoint);rejected([&]{(void)discovery::verification::run(model,input,options,native,modules);},"Truncated verification ledgers are rejected");check(runtime::dataio::readBytes(options.checkpoint)==corrupt,"Corrupt evidence is retained for inspection");
        options.resume=false;options.overwriteCheckpoint=true;options.checkpoint=directory;const auto failedWrite=discovery::verification::run(model,input,options,native,modules);check(!failedWrite.checkpointOkay&&!failedWrite.checkpointMessage.empty()&&failedWrite.rows.size()==3,"Checkpoint write failure reports diagnostics without discarding numerical evidence");
    }catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);std::cout<<checks<<" completed-verification-tier checkpoint checks passed.\n";
}
