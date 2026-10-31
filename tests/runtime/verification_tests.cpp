#include "discovery/verification.hpp"
#include "runtime/interpreter.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
int checks=0,failures=0;
void check(bool value,const char* text){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
template<class Action>void rejected(Action action,const char* text){try{action();check(false,text);}catch(const std::exception&){check(true,text);}}
penta::language::OdeDefinition model(const std::string& source){const auto a=penta::language::analyse(source);if(!a.ok())throw std::runtime_error("Verification fixture did not parse.");return *a.program.statements.front().ode;}
penta::discovery::Options inputs(std::size_t n,std::vector<double> initial){penta::discovery::Options o;o.runs=initial.size()/n;o.physicalCoordinates=n;o.initial=std::move(initial);o.gamma.assign(o.runs,1);o.threads=4;o.solver.t1=1;o.solver.sampleStep=.02;o.solver.rtol=1e-8;o.solver.atol=1e-10;o.solver.method=penta::ode::Method::Bdf;o.maximumWallSeconds=120;return o;}
#ifndef PENTA_VERIFICATION_NATIVE_ONLY
void language(const std::string& source,const penta::runtime::ExecutionOptions& o,bool expected=true){const auto result=penta::runtime::execute(source,{},o);check(result.ok()==expected,"Verification language accepts/rejects the expected contract.");if(result.ok()!=expected){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}}
#endif
}
int main(int argc,char** argv){try{
    const auto directory=std::filesystem::temp_directory_path()/("penta-verification-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    penta::ode::NativeOptions native;native.cacheDirectory=directory/"kernels";std::size_t compilations=0;native.compilationFinished=[&](double){++compilations;};
    const auto modules=argc>1?std::filesystem::absolute(argv[1]):std::filesystem::path{};
    const auto oscillator=model("ode oscillator(t,real x,real v){x'=v+(1-x*x-v*v)*x;v'=-x+(1-x*x-v*v)*v;}");
    auto o=inputs(2,{1,0,0,1});o.returnTolerance=.01;o.minReturnTime=1;
    penta::discovery::verification::Options v;v.runIds={7,3};v.horizons={20,40};v.refinePeriodic=true;
    auto periodic=penta::discovery::verification::run(oscillator,o,v,native,modules);
    check(periodic.rows.size()==4&&periodic.completed==4,"Every selected run has an explicit row for each horizon.");
    check(periodic.rows[0].runId==7&&periodic.rows[1].runId==3&&periodic.rows[2].tierId==2,"Original run IDs and tier ordering survive worker scheduling.");
    for(const auto& row:periodic.rows){if(!row.periodicConverged)std::cerr<<"Oscillator evidence: tier="<<row.tierId<<", screen="<<row.screenStatus<<", period="<<row.period<<", closure="<<row.closureResidual<<", diagnostic="<<row.diagnosticStatus<<", message="<<row.message<<'\n';check(row.periodicConverged,"Known oscillator cycle has a refined closure and neutral multiplier.");check(std::abs(row.period-2*std::acos(-1.0))<1e-6,"Refined oscillator period matches the analytic solution.");check(row.closureResidual<1e-7,"Known cycle closure is numerically small.");check(row.floquetModuli.size()==2&&std::abs(row.floquetModuli.front()-1)<1e-5,"Oscillator Floquet multipliers agree with unity.");}
    check(compilations==1,"Every tier/run and shooting call reuses the one resident equation kernel.");
    check(periodic.parallel.threadsUsed>=2&&periodic.parallel.threadsUsed<=4,"Verification uses the shared bounded numerical worker budget.");
    auto serial=o;serial.threads=1;const auto reference=penta::discovery::verification::run(oscillator,serial,v,native,modules);
    for(std::size_t k=0;k<periodic.rows.size();++k)check(std::abs(periodic.rows[k].period-reference.rows[k].period)<1e-12,"Serial/parallel evidence is independent of scheduling.");
    const auto auxiliary=model("ode withwork(t,real x,real v,real W){x'=v+(1-x*x-v*v)*x;v'=-x+(1-x*x-v*v)*v;W'=1.0;}");
    auto aux=inputs(3,{1,0,0});aux.physicalCoordinates=2;aux.returnTolerance=.01;aux.minReturnTime=1;auto av=v;av.runIds={21};
    const auto pruned=penta::discovery::verification::run(auxiliary,aux,av,native,modules);
    check(pruned.originalKey!=pruned.physicalKey&&pruned.physicalCoordinates==2,"Independent work integrals are excluded using a checked physical kernel.");
    check(pruned.rows.back().periodicConverged&&std::abs(pruned.rows.back().state[2]-40)<1e-7,"Pruning diagnostics preserves the complete original stored trajectory endpoint.");
    rejected([&]{(void)penta::discovery::verification::physicalModel(model("ode coupled(t,real x,real W){x'=W;W'=x;}"),1);},"A physical RHS cannot reference an excluded auxiliary.");
    rejected([&]{(void)penta::discovery::verification::physicalModel(model("ode p(t,pentarnion q){q'=-q;}"),3);},"A Pentarnion state cannot be split to choose a physical dimension.");
    const auto forced=model("ode forced(t,real x,real y){x'=cos(t);y'=sin(t);}");auto f=inputs(2,{0,0});f.returnTolerance=.03;f.minReturnTime=1;auto fv=v;fv.runIds={1};
    const auto forcing=penta::discovery::verification::run(forced,f,fv,native,modules);
    check(!forcing.rows.back().periodicConverged&&forcing.rows.back().diagnosticStatus=="nonautonomous_shooting_unsupported","Nonautonomous Floquet shooting is explicitly unsupported.");
    const auto diagonal=model("ode diagonal(t,real x,real y){x'=-x;y'=-2*y;}");auto d=inputs(2,{1,1});auto dv=v;dv.runIds={1};dv.horizons={4,8};dv.refinePeriodic=false;dv.lyapunov=true;dv.minimumObservation=1;dv.qrStep=.25;dv.lyapunovTolerance=1e-5;
    const auto stable=penta::discovery::verification::run(diagonal,d,dv,native,modules);
    for(const auto& row:stable.rows){check(row.lyapunovAvailable&&std::abs(row.exponents[0]+1)<1e-6&&std::abs(row.exponents[1]+2)<1e-6,"Native tangent QR reproduces exact diagonal exponents.");check(row.windowConverged&&row.settingsConverged,"Independent method, tighter tolerance and half-QR checks agree for an analytic system.");check(row.status!=10&&!row.positive,"Stable exponents do not become a chaotic candidate.");}
    const auto weak=model("ode weak(t,real x,real y){x'=0.005*x;y'=-y;}");auto weakOptions=d;auto wv=dv;wv.minimumPositive=.02;
    const auto weakEvidence=penta::discovery::verification::run(weak,weakOptions,wv,native,modules);
    check(weakEvidence.rows.back().largest>0&&weakEvidence.rows.back().largest<.02&&weakEvidence.rows.back().label=="inconclusive","Weak finite-time positivity remains inconclusive.");
    const auto saddle=model("ode saddle(t,real x,real y){x'=x;y'=-y;}");const auto saddleEvidence=penta::discovery::verification::run(saddle,d,dv,native,modules);
    check(saddleEvidence.rows.back().largest>.9&&saddleEvidence.rows.back().windowConverged&&saddleEvidence.rows.back().settingsConverged&&saddleEvidence.rows.back().status!=10,"Positive converged exponential growth without full-state recurrence is not labelled chaotic.");
    const auto torus=model("ode torus(t,real x,real vx,real y,real vy){x'=vx;vx'=-x;y'=vy;vy'=-2*y;}");auto torusInput=inputs(4,{1,0,1,0});auto tv=dv;tv.horizons={20,40};tv.minimumObservation=10;tv.lyapunovTolerance=.1;
    const auto quasiperiodic=penta::discovery::verification::run(torus,torusInput,tv,native,modules);
    check(std::abs(quasiperiodic.rows.back().largest)<.04&&quasiperiodic.rows.back().status!=10,"Incommensurate analytic oscillators have near-zero finite-time growth, without a chaos claim.");
    const auto lorenz=model("ode lorenz(t,real x,real y,real z){x'=10*(y-x);y'=x*(28-z)-y;z'=x*y-(8.0/3)*z;}");auto lorenzInput=inputs(3,{1,1,1});lorenzInput.transient=10;lorenzInput.returnTolerance=.15;lorenzInput.minReturnTime=1;
    auto lv=dv;lv.horizons={40,80};lv.minimumObservation=20;lv.qrStep=.5;lv.lyapunovTolerance=.4;lv.minimumPositive=.1;
    const auto chaotic=penta::discovery::verification::run(lorenz,lorenzInput,lv,native,modules);const auto& lorenzFinal=chaotic.rows.back();
    check(lorenzFinal.lyapunovAvailable&&lorenzFinal.largest>.3&&lorenzFinal.largest<1.5,"Lorenz reference has a positive native finite-time Lyapunov estimate in the expected broad range.");
    double exponentSum=0;for(const double exponent:lorenzFinal.exponents)exponentSum+=exponent;
    check(std::abs(exponentSum+(10+1+8.0/3))<1e-4,"Lorenz tangent spectrum sum matches the exact constant divergence.");
    check(lorenzFinal.status!=10||(lorenzFinal.tierConverged&&lorenzFinal.settingsConverged&&lorenzFinal.windowConverged&&lorenzFinal.positive),"A chaotic candidate exposes all required finite-time convergence flags.");
    std::cout<<"Lorenz finite-time largest="<<lorenzFinal.largest<<", window drift="<<lorenzFinal.windowDrift<<", method/QR drift="<<lorenzFinal.settingDrift<<", tier drift="<<lorenzFinal.tierDrift<<", label="<<lorenzFinal.label<<'\n';
    auto invalid=dv;invalid.horizons={.5};rejected([&]{(void)penta::discovery::verification::run(diagonal,d,invalid,native,modules);},"Verification does not shorten the original horizon.");
    invalid=dv;invalid.horizons={4,4};rejected([&]{(void)penta::discovery::verification::run(diagonal,d,invalid,native,modules);},"Follow-up horizons must increase.");
    invalid=dv;invalid.maximumIntervals=2;rejected([&]{(void)penta::discovery::verification::run(diagonal,d,invalid,native,modules);},"Tangent preflight rejects an insufficient interval budget.");
    auto stopped=native;stopped.cancellation=std::make_shared<std::atomic_bool>(true);const auto cancelled=penta::discovery::verification::run(diagonal,d,dv,stopped,modules);check(cancelled.cancelled,"Verification supports cooperative cancellation.");
#ifndef PENTA_VERIFICATION_NATIVE_ONLY
    penta::runtime::ExecutionOptions execution;execution.dataDirectory=directory;execution.odeCacheDirectory=native.cacheDirectory;execution.numericModuleDirectory=modules;
    language("ode contract(t,real x){x'=-x;}record e=discover(contract,[1;2],options(t1=1.0,sample_step=0.1));record v=verifyexperiment(contract,e,[2,1],options(horizons=[2,4],rtol=1e-9,atol=1e-11));assert(v.rows==4);assert(v.run_ids==[2;1;2;1]);assert(v.rtols(3)<v.rtols(1));assert(v.source_request_hash==e.request_hash);record replay=verifyrun(contract,v,3);assert(replay.run_id==2);assert(abs(replay.values(end,1)-2*exp(-4))<1e-7);saveanalysis(\"evidence.json\",v);record saved=loadanalysis(\"evidence.json\",v);assert(saved.run_ids==v.run_ids);",execution);
    language("ode p(t,pentarnion q){q'=-q;}record e=discover(p,[1,2,3,4,5],options(t1=.1,sample_step=.1,gammas=[0,1]));record v=verifyexperiment(p,e,[1,2],options(horizons=[.2,.4]));record replay=verifyrun(p,v,1);assert(replay.gamma==0);assert(replay.coordinates==5);",execution);
    language("ode model(t,real x){x'=-x;}record e=discover(model,[1],options(t1=1.0));record v=verifyexperiment(model,e,[1,1]);",execution,false);
    language("ode model(t,real x){x'=-x;}record e=discover(model,[1],options(t1=1.0));record v=verifyexperiment(model,e,[1],options(rtol=.01));",execution,false);
    language("ode first(t,real x){x'=-x;}ode second(t,real x){x'=-2*x;}record e=discover(first,[1],options(t1=1.0));record v=verifyexperiment(second,e,[1]);",execution,false);
#endif
}catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}
    std::cout<<checks<<" tiered verification checks, "<<failures<<" failures\n";return failures?1:0;
}
