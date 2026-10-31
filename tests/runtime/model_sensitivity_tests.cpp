#include "sensitivity/solver_summary.hpp"
#include "runtime/interpreter.hpp"
#include "algebra/pentarnion.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
int checks=0,failures=0;void check(bool okay,const char* text){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
template<class Action>void rejected(Action action,const char* text){try{action();check(false,text);}catch(const std::exception&){check(true,text);}}
penta::language::AnalysisResult source(const std::string& text){auto a=penta::language::analyse(text);if(!a.ok())throw std::runtime_error("Model sensitivity fixture did not parse.");return a;}
#ifndef PENTA_MODEL_SENSITIVITY_NATIVE_ONLY
void language(const std::string& text,const penta::runtime::ExecutionOptions& options,bool expected=true){const auto result=penta::runtime::execute(text,{},options);check(result.ok()==expected,"Native solver sensitivity language accepts/rejects the expected contract.");if(result.ok()!=expected){std::cerr<<text<<'\n';for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}}
#endif
}
int main(int argc,char** argv){try{
    using namespace penta::sensitivity::solver;
    const auto directory=std::filesystem::temp_directory_path()/("penta-model-sensitivity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto modules=argc>1?std::filesystem::absolute(argv[1]):std::filesystem::path{};penta::ode::NativeOptions native;native.cacheDirectory=directory/"kernels";
    auto odeDefinition=source("ode affine(t,real x;real a,real b){x'=a+2*b;}");
    Model ode;ode.ode=std::make_shared<penta::ode::PreparedModel>(*odeDefinition.program.statements.front().ode,native);ode.initial={0};ode.parameters={0,0};ode.parameterCoordinates={0,1};ode.stateCoordinates=1;ode.native=native;ode.moduleDirectory=modules;ode.odeOptions.t1=1;ode.odeOptions.sampleStep=.5;ode.odeOptions.rtol=1e-10;ode.odeOptions.atol=1e-12;
    const std::vector<double> point{.2,.3};ode.validate(2);check(std::abs(ode.evaluate(point)-.8)<1e-9,"Native ODE final summary matches an analytic affine solution.");
    auto mean=ode;mean.summary=Summary::Mean;check(std::abs(mean.evaluate(point)-.4)<1e-9,"ODE mean uses the documented discrete stored samples.");
    check(ode.estimatedBufferBytes()>=8*(16*ode.stateCoordinates*ode.stateCoordinates+256*ode.stateCoordinates),"Concurrent buffer forecasting includes dense Jacobian/LU and native solver vectors.");
    auto rms=ode;rms.summary=Summary::Rms;check(std::abs(rms.evaluate(point)-std::sqrt((.16+.64)/3))<1e-9,"ODE sample RMS has a known analytic reference.");
    const std::vector<double> lower{0,0},upper{1,1};penta_sensitivity_request_v1 request{};request.struct_size=sizeof(request);request.dimensions=2;request.method=PENTA_SENS_MORRIS;request.samples=12;request.seed=42;request.levels=6;request.max_evaluations=100000;request.confidence=.95;request.max_wall_seconds=120;request.lower=lower.data();request.upper=upper.data();
    const auto morris=analyze(ode,request,{},4);check(std::abs(morris.sensitivity.mu[0]-1)<1e-7&&std::abs(morris.sensitivity.mu[1]-2)<1e-7,"Morris effects of an analytic ODE summary are exact.");
    check(morris.sensitivity.statistics.threadsUsed>=1&&morris.sensitivity.statistics.threadsUsed<=4&&morris.sensitivity.threadsGranted<=4,"Small solver summary batches respect the shared worker budget.");
    const auto serial=analyze(ode,request,{},1);check(serial.sensitivity.mu==morris.sensitivity.mu&&serial.sensitivity.sigma==morris.sensitivity.sigma,"Serial/parallel solver summary identities are deterministic.");
    request.method=PENTA_SENS_SOBOL;request.samples=1024;const auto sobol=analyze(ode,request,{},4);
    check(std::abs(sobol.sensitivity.first[0]-.2)<.08&&std::abs(sobol.sensitivity.first[1]-.8)<.08,"Sobol ODE estimates agree with the independent additive variance decomposition.");
    check(std::abs(sobol.sensitivity.total[0]-.2)<.08&&std::abs(sobol.sensitivity.total[1]-.8)<.08,"Sobol total ODE effects agree with the analytic reference.");
    check(sobol.sensitivity.statistics.threadsUsed>1&&sobol.sensitivity.statistics.threadsUsed<=4,"The larger ODE summary workload executes on multiple shared workers.");
    auto mapDefinition=source("native function real affine(real x,real a,real b){return x+a+2*b;}");penta::native_functions::Options callbackOptions;callbackOptions.build=native;
    Model map;map.kind=Kind::Map;map.callback=std::make_shared<penta::native_functions::Callback>(*mapDefinition.program.statements.front().function,callbackOptions);map.initial={0};map.parameters={0,0};map.parameterCoordinates={0,1};map.mapIterations=5;map.native=native;map.moduleDirectory=modules;map.validate(2);
    check(std::abs(map.evaluate(point)-4)<1e-12,"Native map summary uses all requested iterations without interpretation.");
    request.method=PENTA_SENS_MORRIS;request.samples=8;const auto mapEffects=analyze(map,request,{},4);check(std::abs(mapEffects.sensitivity.mu[0]-5)<1e-10&&std::abs(mapEffects.sensitivity.mu[1]-10)<1e-10,"Map sensitivity effects match the exact iterated affine map.");
    auto reaction=source("native function real reaction(real u,real t,real x,real a,real b){return a+2*b;}");
    Model pde;pde.kind=Kind::Pde1;pde.summary=Summary::SpatialMean;pde.callback=std::make_shared<penta::native_functions::Callback>(*reaction.program.statements.front().function,callbackOptions);pde.initial.assign(8,0);pde.x={0,.125,.25,.375,.5,.625,.75,.875};pde.left={0};pde.right={0};pde.parameters={0,0,.1};pde.parameterCoordinates={0,1};pde.reactionParameters=2;pde.native=native;pde.moduleDirectory=modules;auto& r=pde.pde.base;r.struct_size=sizeof(pde.pde);r.nodes=8;r.components=1;r.length=1;r.diffusion=.1;r.t1=.1;r.time_step=.01;r.sample_step=.1;r.max_steps=10000;r.max_reaction_evaluations=100000;r.max_wall_seconds=30;
    pde.validate(2);check(std::abs(pde.evaluate(point)-.08)<1e-12,"Spatially constant FD reaction-diffusion mean matches the analytic solution.");
    const auto pdeEffects=analyze(pde,request,{},4);check(std::abs(pdeEffects.sensitivity.mu[0]-.1)<1e-10&&std::abs(pdeEffects.sensitivity.mu[1]-.2)<1e-10,"PDE summary Morris effects match the independent constant-field reference.");
    pde.pde.base.method=1;check(std::abs(pde.evaluate(point)-.08)<1e-12,"Consistent-mass FE spatial summaries retain the same constant solution.");
    auto reaction2=source("native function real reaction2(real u,real t,real x,real y,real a){return a;}");auto pde2=pde;pde2.kind=Kind::Pde2;pde2.stateCoordinates=1;pde2.callback=std::make_shared<penta::native_functions::Callback>(*reaction2.program.statements.front().function,callbackOptions);pde2.x={0,.25,.5,.75};pde2.y=pde2.x;pde2.initial.assign(16,0);pde2.boundary={0};pde2.parameters={0,.1,.1};pde2.parameterCoordinates={0,1,2};pde2.reactionParameters=1;auto& g=pde2.grid;g.struct_size=sizeof(g);g.abi_version=PENTA_PDE_GRID_ABI_V1;g.nx=g.ny=4;g.components=1;g.length_x=g.length_y=1;g.t1=.1;g.time_step=.01;g.sample_step=.1;g.max_steps=10000;g.max_reaction_evaluations=100000;g.max_wall_seconds=30;
    pde2.validate(3);const std::vector<double> pdePoint{.4,.1,.15};check(std::abs(pde2.evaluate(pdePoint)-.04)<1e-12,"Two-dimensional constant reaction-diffusion summary matches the analytic field.");
    const std::vector<double> lower2{0,.05,.05},upper2{1,.15,.15};auto request2=request;request2.dimensions=3;request2.lower=lower2.data();request2.upper=upper2.data();const auto effects2=analyze(pde2,request2,{},4);
    check(std::abs(effects2.sensitivity.mu[0]-.1)<1e-10&&std::abs(effects2.sensitivity.mu[1])<1e-10&&std::abs(effects2.sensitivity.mu[2])<1e-10,"PDE2 reaction and appended diffusion coordinates have the expected independent effects.");
    auto orderedDefinition=source("native function pentarnion ordered(pentarnion p,pentarnion A){return A*p;}");Model ordered;ordered.kind=Kind::Map;ordered.stateCoordinates=5;ordered.coefficient=3;ordered.mapIterations=1;ordered.initial={1,.2,-.3,.4,-.5};ordered.parameters={.1,.2,.3,.4,.5};ordered.parameterCoordinates={1,4};ordered.native=native;ordered.callback=std::make_shared<penta::native_functions::Callback>(*orderedDefinition.program.statements.front().function,callbackOptions);ordered.moduleDirectory=modules;ordered.validate(2);
    using namespace penta::algebra;const Pentarnion P(Context(1),{1,.2,-.3,.4,-.5}),A(Context(1),{.1,point[0],.3,.4,point[1]});
    check(std::abs(ordered.evaluate(point)-multiply(A,P).coefficients()[3])<1e-13,"Ordered Pentarnion map sensitivity preserves every coefficient and left multiplication.");
    check(std::abs(multiply(A,P).coefficients()[3]-multiply(P,A).coefficients()[3])>1e-3,"Noncommuting reference distinguishes left and right products.");
    auto pentaReaction=source("native function pentarnion penta_reaction(pentarnion u,real t,real x,real a){return a*(j*u);}");auto pentaPde=pde;pentaPde.pde.base.method=0;pentaPde.pde.base.components=5;pentaPde.stateCoordinates=5;pentaPde.callback=std::make_shared<penta::native_functions::Callback>(*pentaReaction.program.statements.front().function,callbackOptions);pentaPde.initial.clear();for(std::size_t node=0;node<8;++node)pentaPde.initial.insert(pentaPde.initial.end(),{1,.2,-.3,.4,-.5});pentaPde.left.assign(5,0);pentaPde.right.assign(5,0);pentaPde.parameters={0,.1};pentaPde.parameterCoordinates={0};pentaPde.reactionParameters=1;pentaPde.summary=Summary::Final;
    const auto orderedOdeDefinition=source("ode ordered_field(t,pentarnion q;real a){q'=a*(j*q);}");penta::ode::PreparedModel odeReference(*orderedOdeDefinition.program.statements.front().ode,native);penta::ode::SolveOptions tight;tight.t1=.1;tight.sampleStep=.1;tight.rtol=1e-12;tight.atol=1e-14;const auto pentaReference=odeReference.solve(1,ordered.initial,{.7},tight,native);const std::vector<double> pentaPoint{.7};
    for(std::size_t coefficient=0;coefficient<5;++coefficient){pentaPde.coefficient=coefficient;pentaPde.validate(1);check(std::abs(pentaPde.evaluate(pentaPoint)-pentaReference->values[5+coefficient])<1e-8,"All five ordered Pentarnion PDE summary coefficients match an independent native ODE reference.");}
    auto invalid=ode;invalid.parameterCoordinates={0,0};rejected([&]{invalid.validate(2);},"Repeated parameter coordinates are rejected.");
    invalid=ode;invalid.maximumBytes=1024;invalid.odeOptions.sampleStep=1e-6;rejected([&]{invalid.validate(2);},"Native summary sample buffers respect the memory budget.");
    auto badDefinition=source("native function real invalid(real x,real a,real b){return sqrt(a-2);}");auto bad=map;bad.callback=std::make_shared<penta::native_functions::Callback>(*badDefinition.program.statements.front().function,callbackOptions);rejected([&]{(void)analyze(bad,request,{},4);},"Failed native solve/callback aborts sensitivity without zero imputation.");
    auto cancel=std::make_shared<std::atomic_bool>(true);rejected([&]{(void)analyze(ode,request,cancel,4);},"Global solver sensitivity supports cooperative cancellation.");
    const auto originalHash=ode.fingerprint();ode.odeOptions.rtol*=.1;check(ode.fingerprint()!=originalHash,"Runtime numerical settings change scientific identity without changing the compiled body.");
#ifndef PENTA_MODEL_SENSITIVITY_NATIVE_ONLY
    penta::runtime::ExecutionOptions options;options.dataDirectory=directory;options.odeCacheDirectory=native.cacheDirectory;options.numericModuleDirectory=modules;
    language("ode affine(t,real x;real a,real b){x'=a+2*b;}record r=modelsensitivity(affine,[0,0],[1,1],options(initial=[0],parameters=options(a=0.0,b=0.0),method=\"morris\",samples=4,threads=4,solver=options(t1=1.0,sample_step=.5)));assert(norm(r.mu_star-[1,2])<1e-6);assert(r.kind==\"ode\");assert(r.threads_used<=4);",options);
    language("native function real affine(real x,real a,real b){return x+a+2*b;}record r=modelsensitivity(@affine,[0,0],[1,1],options(kind=\"map\",initial=[0],parameters=options(a=0.0,b=0.0),method=\"morris\",samples=4,solver=options(iterations=5)));assert(norm(r.mu_star-[5,10])<1e-8);",options);
    language("native function real reaction(real u,real t,real x,real a){return a;}record r=modelsensitivity(@reaction,[0],[1],options(kind=\"pde1\",initial=zeros(1,8),x=0:.125:.875,parameters=options(a=0.0),parameter_coordinates=[1],summary=\"spatial_mean\",method=\"morris\",samples=4,solver=options(t1=.1,time_step=.01,sample_step=.1,diffusion=.1)));assert(abs(r.mu_star(1)-.1)<1e-8);",options);
    language("native function real reaction(real u,real t,real x,real y,real a){return a;}record r=modelsensitivity(@reaction,[0,.05,.05],[1,.15,.15],options(kind=\"pde2\",initial=zeros(1,16),x=0:.25:.75,y=0:.25:.75,parameters=options(a=0.0),summary=\"spatial_mean\",method=\"morris\",samples=4,solver=options(t1=.1,time_step=.01,sample_step=.1)));assert(norm(r.mu_star-[.1,0,0])<1e-8);",options);
    language("ode affine(t,real x;real a){x'=a;}record r=modelsensitivity(affine,[0],[1],options(initial=[0],parameters=options(a=0.0),parameter_coordinates=[2]));",options,false);
#endif
}catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}
std::cout<<checks<<" native solver sensitivity checks, "<<failures<<" failures\n";return failures?1:0;
}
