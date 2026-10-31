#include "discovery/discovery_module.hpp"
#include "runtime/data_io.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
namespace {
int checks=0,failures=0;
void require(bool value,const char* text){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
template<class F>void rejects(F action,const char* text){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}require(rejected,text);}
std::shared_ptr<const penta::language::OdeDefinition> model(const char* text){const auto source=penta::language::analyse(text);if(!source.ok())throw std::runtime_error("Discovery test model did not check.");return source.program.statements.front().ode;}
}
int main(){try{
    const auto directory=std::filesystem::temp_directory_path()/("penta-discovery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    penta::ode::NativeOptions native;native.cacheDirectory=directory/"kernels";std::size_t compilations=0;native.compilationFinished=[&](double){++compilations;};
    penta::ode::PreparedModel oscillator(*model("ode oscillator(t,real x,real v){x'=v;v'=-x;}"),native);
    penta::discovery::Module module;
    penta::discovery::Options o;o.native=native;o.runs=8;o.physicalCoordinates=2;o.threads=1;o.batchSize=8;o.solver.t1=20;o.solver.sampleStep=.02;o.solver.rtol=1e-10;o.solver.atol=1e-12;o.returnTolerance=.01;o.minReturnTime=1;
    for(std::size_t row=0;row<o.runs;++row){o.initial.push_back(1+.1*row);o.initial.push_back(.1*row);o.gamma.push_back(double(row)/8);}
    const auto serial=module.run(oscillator,o);require(serial.completed==8,"Native serial candidate completion.");require(compilations==1,"One native kernel preparation for all candidates.");
    o.threads=4;const auto parallel=module.run(oscillator,o);require(parallel.completed==8,"Parallel candidate completion.");require(parallel.parallel.threadsUsed>1,"Native search actually uses pool workers.");
    for(std::size_t row=0;row<o.runs;++row){
        require(serial.status[row]==PENTA_DISCOVERY_PERIODIC,"Known harmonic orbit is a periodic candidate.");
        require(serial.status[row]==parallel.status[row],"Deterministic finite-time classifications.");
        for(std::size_t k=0;k<2;++k)require(serial.states[row*2+k]==parallel.states[row*2+k],"Bit-identical independent run results for this backend/build.");
        const double expected=(1+.1*row)*std::cos(20)+.1*row*std::sin(20);
        require(std::abs(serial.states[row*2]-expected)<1e-7,"Independent analytic harmonic solution.");
        require(std::abs(serial.period[row]-2*std::acos(-1))<.1,"Full-state return period candidate.");
    }
    o.checkpoint=directory/"experiment.json";o.batchSize=2;o.native.cancellation=std::make_shared<std::atomic_bool>(false);
    o.progress=[&](std::size_t done,std::size_t){if(done>=4)o.native.cancellation->store(true);};
    const auto stopped=module.run(oscillator,o);require(stopped.cancelled&&stopped.completed==4,"Cancellation leaves completed-task ledger.");
    require(std::filesystem::exists(o.checkpoint),"Atomic checkpoint is written on cancellation.");
    o.native.cancellation=std::make_shared<std::atomic_bool>(false);o.progress={};o.resume=true;
    const auto resumed=module.run(oscillator,o);require(resumed.restored==4&&resumed.completed==8,"Resume skips completed tasks.");
    require(resumed.status==serial.status&&resumed.states==serial.states,"Resumed scientific results equal uninterrupted reference.");
    auto changed=o;changed.initial[0]+=.1;rejects([&]{(void)module.run(oscillator,changed);},"Changed inputs reject old checkpoint.");
    auto all=module.run(oscillator,o);require(all.restored==8&&all.completed==8,"Completed checkpoint replay avoids solver work.");
    auto malformed=o;malformed.gamma[0]=std::numeric_limits<double>::quiet_NaN();rejects([&]{(void)module.run(oscillator,malformed);},"Invalid gamma preflight.");
    malformed=o;malformed.maximumBytes=1024;rejects([&]{(void)module.run(oscillator,malformed);},"Result memory budget preflight.");
    penta::ode::PreparedModel growth(*model("ode growth(t,real x){x'=x;}"),native);
    penta::discovery::Options threshold;threshold.native=native;threshold.runs=1;threshold.physicalCoordinates=1;threshold.initial={1};threshold.gamma={1};threshold.escapeRadius=2;threshold.solver.t1=10;threshold.solver.rtol=1e-11;threshold.solver.atol=1e-13;
    const auto escaped=module.run(growth,threshold);require(escaped.status[0]==PENTA_DISCOVERY_THRESHOLD,"Cutoff is threshold_reached, never blow-up proof.");
    require(std::abs(escaped.stopTimes[0]-std::log(2))<1e-8,"Threshold uses accurately localized solver event.");
    penta::ode::PreparedModel domain(*model("ode domain(t,real x;real a){x'=sqrt(a);}"),native);
    penta::discovery::Options isolated;isolated.native=native;isolated.runs=2;isolated.physicalCoordinates=1;isolated.initial={0,0};isolated.parameters={1,-1};isolated.gamma={1,1};isolated.threads=2;isolated.solver.t1=.1;
    const auto failed=module.run(domain,isolated);require(failed.status[0]!=PENTA_DISCOVERY_FAILURE&&failed.status[1]==PENTA_DISCOVERY_FAILURE,"One invalid native candidate is isolated.");
    require(!failed.messages[1].empty(),"Numerical failure keeps diagnostic text.");
    require(std::string(penta::discovery::statusName(PENTA_DISCOVERY_BOUNDED))=="bounded_over_horizon","Finite-time status wording.");
}catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
std::cout<<checks<<" native discovery checks, "<<failures<<" failures\n";return failures?1:0;}
