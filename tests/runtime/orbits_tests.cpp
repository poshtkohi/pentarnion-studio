#include "orbits/orbits_module.hpp"
#ifndef PENTA_ORBITS_DRIVER_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0,failures=0;
void expect(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
bool near(double actual,double expected,double error){return std::abs(actual-expected)<=error;}
penta::language::OdeDefinition model(const std::string& source){
    const auto checked=penta::language::analyse(source);
    if(!checked.ok()){for(const auto& d:checked.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';throw std::runtime_error("Test ODE source did not check.");}
    return *checked.program.statements.front().ode;
}
void driver(const std::filesystem::path& temporary){
    using namespace penta;const double pi=std::acos(-1.0);
    const auto normal=model("ode cycle(t,real x,real y;real mu){x'=(mu-x*x-y*y)*x-y;y'=x+(mu-x*x-y*y)*y;}");
    ode::NativeOptions native;native.cacheDirectory=temporary/"native";std::size_t compiles=0,charged=0;
    native.compilationFinished=[&](double){++compiles;};native.chargeOperations=[&](std::size_t n){charged+=n;};
    orbits::PeriodicOptions options;options.period=6.1;options.tolerance=1e-8;options.maximumSeconds=60;
    options.solver.rtol=1e-11;options.solver.atol=1e-13;options.solver.method=ode::Method::Adams;
    const auto cycle=orbits::periodicOrbit(normal,1,{1.1,0},{1},options,native);
    expect(cycle.converged&&cycle.status=="periodic_candidate","shooting refines the nontrivial Hopf normal-form orbit");
    expect(near(cycle.period,2*pi,2e-7)&&cycle.closureResidual<1e-8,"refined period and full state closure match independent analytic cycle");
    expect(cycle.stable&&!cycle.unstable&&!cycle.inconclusive,"transverse Floquet multiplier establishes numerical orbital stability");
    expect(cycle.neutralError<2e-7&&near(cycle.multipliersModulus[1],std::exp(-4*pi),2e-8),"neutral and analytic radial Floquet multiplier distinguished");
    expect(cycle.orbit->coordinateCount()==2&&cycle.orbit->times.size()>=200,"full-state sampled periodic orbit retained");
    const auto x=cycle.state[0],y=cycle.state[1],a=std::exp(-4*pi);
    const std::vector<double> phi{1+(a-1)*x*x,(a-1)*x*y,(a-1)*x*y,1+(a-1)*y*y};
    double monodromyError=0;for(std::size_t k=0;k<4;++k)monodromyError=std::max(monodromyError,std::abs(phi[k]-cycle.monodromy[k]));
    expect(monodromyError<3e-7,"generated tangent monodromy matches independent analytic radial/tangential projection");
    auto largePhase=options;largePhase.phaseReference={1,0};largePhase.phaseDirection={1e308,1e308};
    const auto hugeDirection=orbits::periodicOrbit(normal,1,{1.1,0},{1},largePhase,native);
    expect(hugeDirection.converged&&hugeDirection.phaseResidual<1e-8&&near(hugeDirection.state[0],1,1e-7)&&near(hugeDirection.state[1],0,1e-7),"large finite phase direction preserves the analytic circle-section intersection");
    const auto stationary=model("ode stationary(t,real x,real y){x'=0.0;y'=0.0;}");auto extremePhase=options;extremePhase.period=1;extremePhase.phaseReference={-1e308,-1e308};extremePhase.phaseDirection={1,-.5};
    const auto finitePhase=orbits::periodicOrbit(stationary,1,{1e308,1e308},{},extremePhase,native);
    expect(!finitePhase.converged&&std::isfinite(finitePhase.phaseResidual)&&std::abs(finitePhase.phaseResidual/(1e308/std::sqrt(1.25))-1)<1e-14,"opposite extreme phase coordinates retain their finite nonzero section residual");
    bool unrepresentablePhase=false;try{extremePhase.phaseDirection={1,1};(void)orbits::periodicOrbit(stationary,1,{1e308,1e308},{},extremePhase,native);}catch(const std::exception&){unrepresentablePhase=true;}expect(unrepresentablePhase,"unrepresentable phase residual rejects before a convergence decision");
    const auto firstCompiles=compiles;options.period=cycle.period;
    const auto changed=orbits::periodicOrbit(normal,.5,{std::sqrt(2.),0},{2},options,native);
    expect(changed.cacheHit&&changed.key==cycle.key&&compiles==firstCompiles,"parameter/gamma/initial/period changes reuse structural ODE binary");
    expect(charged>0,"shooting charges invoking owner's native work budget");
    auto shortRun=options;shortRun.maximumIterations=1;shortRun.period=5.1;
    const auto unfinished=orbits::periodicOrbit(normal,1,{1.3,0},{1},shortRun,native);
    expect(!unfinished.converged&&unfinished.inconclusive&&!unfinished.stable,"incomplete shooting reports nonconvergence without a stability claim");

    const auto scaled=model("ode scaled(t,real x,real y){x'=(1-x*x/1000000-y*y/0.000001)*x-1000000*y;y'=0.000001*x+(1-x*x/1000000-y*y/0.000001)*y;}");
    auto scaledOptions=options;scaledOptions.stateScale={1000,.001};scaledOptions.solver.atol=1e-16;scaledOptions.period=6.1;
    const auto largeUnits=orbits::periodicOrbit(scaled,1,{1050,0},{},scaledOptions,native);
    expect(largeUnits.converged&&largeUnits.scaledClosureResidual<1e-8&&near(largeUnits.period,2*pi,1e-6),"scaled shooting supports state units differing by six orders of magnitude");

    // A center has two unit multipliers. Removing one neutral multiplier must
    // not turn the remaining neutral family direction into a stable result.
    const auto center=model("ode center(t,real x,real y){x'=-y;y'=x;}");
    const auto family=orbits::periodicOrbit(center,1,{1,0},{},options,native);
    expect(family.converged&&family.inconclusive&&!family.stable,"neutral family remains inconclusive after autonomous neutral exclusion");
    auto zeroOptions=options;zeroOptions.phaseDirection={1,0};
    const auto equilibrium=orbits::periodicOrbit(center,1,{0,0},{},zeroOptions,native);
    expect(!equilibrium.converged&&equilibrium.status=="equilibrium_candidate","zero-amplitude equilibrium is not mislabelled as a nontrivial cycle");
    auto reject=[&](auto operation,const char* message){bool caught=false;try{operation();}catch(const std::exception&){caught=true;}expect(caught,message);};
    reject([&]{auto bad=options;bad.stateScale={1,0};(void)orbits::periodicOrbit(normal,1,{1,0},{1},bad,native);},"zero state scaling rejected before native iteration");
    reject([&]{auto bad=options;bad.solver.method=ode::Method::Erk;(void)orbits::periodicOrbit(normal,1,{1,0},{1},bad,native);},"unsupported monodromy integrator rejected explicitly");
    const auto forced=model("ode forced(t,real x,real y){x'=y;y'=-x+sin(t);}");
    reject([&]{(void)orbits::periodicOrbit(forced,1,{1,0},{},options,native);},"time-dependent RHS requires a separate forced-period formulation");
    auto stopped=native;stopped.cancellation=std::make_shared<std::atomic_bool>(true);
    reject([&]{(void)orbits::periodicOrbit(normal,1,{1,0},{1},options,stopped);},"native shooting honors cancellation");
    auto tiny=native;tiny.chargeOperations=[](std::size_t){throw std::runtime_error("test quota");};
    reject([&]{(void)orbits::periodicOrbit(normal,1,{1,0},{1},options,tiny);},"native shooting does not bypass host budget exceptions");

    const auto pent=model("ode pentcycle(t,pentarnion p;real mu){p'=pent([(mu-coeff(p,0)*coeff(p,0)-coeff(p,1)*coeff(p,1))*coeff(p,0)-coeff(p,1),coeff(p,0)+(mu-coeff(p,0)*coeff(p,0)-coeff(p,1)*coeff(p,1))*coeff(p,1),-coeff(p,2),-2*coeff(p,3),-3*coeff(p,4)]);}");
    const auto five=orbits::periodicOrbit(pent,1,{1,0,0,0,0},{1},options,native);
    expect(five.converged&&five.state.size()==5&&five.monodromy.size()==25&&five.orbit->coordinateCount()==5,"one Pentarnion state preserves every real phase coordinate");
    expect(five.stable&&five.multipliersModulus.size()==5,"Floquet calculation includes all five Pentarnion coordinate modes");

    const auto fold=model("ode saddle(t,real x;real mu){x'=mu+x*x;}");
    orbits::ContinuationOptions continuation;continuation.points=36;continuation.step=.1;continuation.maximumStep=.1;continuation.minimumStep=.00001;continuation.tolerance=1e-10;continuation.maximumSeconds=60;
    const auto branch=orbits::continueEquilibrium(fold,1,{-1},{-1},continuation,native);
    expect(branch.completed&&branch.rows==36,"equilibrium pseudo-arclength predictor/corrector completes requested branch");
    double residual=0;bool positive=false,negative=false,foldSeen=false;
    for(std::size_t row=0;row<branch.rows;++row){residual=std::max(residual,std::abs(branch.parameters[row]+branch.states[row]*branch.states[row]));positive=positive||branch.states[row]>.2;negative=negative||branch.states[row]<-.2;foldSeen=foldSeen||branch.foldCandidate[row]!=0;}
    expect(residual<1e-9&&positive&&negative&&foldSeen,"arclength passes the analytic saddle-node fold where parameter stepping fails");
    expect(branch.stable.front()==1&&branch.unstable.back()==1,"real eigenvalues distinguish stable and unstable saddle-node branches");
    auto fineSettings=continuation;fineSettings.points=70;fineSettings.step=.05;fineSettings.maximumStep=.05;
    const auto fine=orbits::continueEquilibrium(fold,1,{-1},{-1},fineSettings,native);
    double coarseFold=1,fineFold=1;
    for(std::size_t row=0;row<branch.rows;++row)if(branch.foldCandidate[row])coarseFold=std::abs(branch.parameters[row]);
    for(std::size_t row=0;row<fine.rows;++row)if(fine.foldCandidate[row])fineFold=std::abs(fine.parameters[row]);
    expect(fine.completed&&fineFold<coarseFold&&fineFold<.003,"halved arclength step tightens the sampled analytic fold candidate");
    const auto hopf=model("ode hopf(t,real x,real y;real mu){x'=mu*x-y;y'=x+mu*y;}");
    continuation.points=14;continuation.step=.05;continuation.maximumStep=.05;
    const auto h=orbits::continueEquilibrium(hopf,1,{0,0},{-.3},continuation,native);
    expect(h.completed&&h.stable.front()==1&&h.unstable.back()==1,"equilibrium continuation follows analytic Hopf stability crossing");
    expect(std::any_of(h.hopfCandidate.begin(),h.hopfCandidate.end(),[](double x){return x!=0;}),"matched complex-pair real-part sign change marks a Hopf candidate");
    continuation.points=6;continuation.step=.1;continuation.maximumStep=.1;continuation.periodic=options;continuation.tolerance=2e-8;
    const auto periodic=orbits::continuePeriodic(normal,1,{1,0},{1},continuation,native);
    expect(periodic.completed&&periodic.rows==6&&periodic.dimension==3,"periodic pseudo-arclength continuation tracks state, period and parameter");
    residual=0;for(std::size_t row=0;row<periodic.rows;++row){const auto u=periodic.states[row*3],v=periodic.states[row*3+1];residual=std::max(residual,std::abs(u*u+v*v-periodic.parameters[row]));expect(near(periodic.periods[row],2*pi,3e-7)&&periodic.closureResiduals[row]<1e-7,"periodic branch period and closure remain analytic");}
    expect(residual<2e-6&&periodic.parameters.back()>periodic.parameters.front(),"periodic branch amplitude follows square root of runtime parameter");
    expect(std::all_of(periodic.stable.begin(),periodic.stable.end(),[](double x){return x==1;}),"branch Floquet evidence remains transversely stable");
    reject([&]{auto bad=continuation;bad.points=1000000000;(void)orbits::continueEquilibrium(fold,1,{-1},{-1},bad,native);},"oversized branch allocation rejected before allocation");
}
#ifndef PENTA_ORBITS_DRIVER_ONLY
void runtime(const std::filesystem::path& temporary){
    using namespace penta::runtime;ExecutionOptions settings;settings.dataDirectory=temporary;settings.odeCacheDirectory=temporary/"language";settings.maxOperations=20000000;
    auto run=[&](const std::string& source,bool good=true){const auto result=execute(source,{},settings);if(result.ok()!=good){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(result.ok()==good,"orbit language result has expected success/failure");return result;};
    run("ode cycle(t,real x,real y;real mu){x'=(mu-x*x-y*y)*x-y;y'=x+(mu-x*x-y*y)*y;}record r=periodicorbit(cycle,[1,0],options(parameters=options(mu=1.0),period=6.283185307179586,rtol=1e-10,atol=1e-12));assert(r.converged&&r.stable);assert(r.closure_residual<1e-6);assert(size(r.states,2)==2);assert(!r.fundamental_period_verified);record b=continueperiodic(cycle,r.x,options(parameters=options(mu=1.0),parameter=\"mu\",period=r.period,points=3,step=0.1,rtol=1e-10,atol=1e-12));assert(b.completed&&b.rows==3);assert(b.parameter(3)>b.parameter(1));assert(!b.bifurcation_refined);");
    run("ode saddle(t,real x;real mu){x'=mu+x*x;}record b=continueequilibrium(saddle,-1.0,options(parameters=options(mu=-1.0),parameter=\"mu\",points=6,step=0.1));assert(b.completed);assert(norm(b.parameter+b.x.*b.x)<1e-6);assert(b.parameter_derivative==\"analytic\");");
    run("ode f(t,real x){x'=-x;}record x=continueequilibrium(f,0.0,options(parameter=\"bad\"));",false);
    run("ode f(t,real x,real y){x'=y;y'=-x;}record x=periodicorbit(f,[1,0],options(typo=1));",false);
    run("record x=periodicorbit([1,0],[1,0]);",false);
#ifdef PENTA_SOURCE_DIR
    for(const auto* file:{"main.p","equilibrium_fold.p"}){const auto path=std::filesystem::path(PENTA_SOURCE_DIR)/"examples"/"periodic-orbits"/file;std::ifstream stream(path);const std::string source((std::istreambuf_iterator<char>(stream)),{});expect(!source.empty(),"meaningfully named runnable orbit example exists");const auto result=run(source);expect(!result.figureActions.empty(),"orbit example retains reproducible figure actions");}
#endif
}
#endif
}
int main(){const auto temporary=std::filesystem::temp_directory_path()/("penta-orbits-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(temporary);
    try{driver(temporary);
#ifndef PENTA_ORBITS_DRIVER_ONLY
        runtime(temporary);
#endif
    }catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION: "<<error.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(temporary,ignored);std::cout<<checks<<" orbit/continuation checks, "<<failures<<" failures\n";return failures?1:0;
}
