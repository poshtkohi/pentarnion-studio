#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace penta;
int main(){
    int checks=0,failed=0;
    auto expect=[&](bool condition,const char* label){++checks;if(!condition){++failed;std::cerr<<"FAILED: "<<label<<'\n';}};
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root=std::filesystem::temp_directory_path()/("penta-build-plan-"+std::to_string(stamp));
    runtime::ExecutionOptions options;options.dataDirectory=root;options.odeCacheDirectory=root/"odes";options.nativeCompileJobs=2;
    const std::string source=R"P(
native function real equation(real x,real a){return x*x-a;}
native function real offset(real x){return x-3.0;}
record first=root(equation,[0.0,2.0],options(parameters=options(a=2.0)));
record second=root(offset,[0.0,4.0]);
assert(first.converged && second.converged);
record changed=root(equation,[0.0,3.0],options(parameters=options(a=3.0)));
assert(changed.cache_hit);
assert(abs(changed.x-sqrt(3.0))<1e-9);
display(first.cache_hit,second.cache_hit);
display(compile_info());
)P";
    auto diagnose=[](const runtime::ExecutionResult& r){for(const auto& d:r.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:r.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';};
    const auto cold=runtime::execute(source,{},options);if(!cold.ok())diagnose(cold);
    std::cerr<<"cold compilation: units="<<cold.nativeCompilation.translationUnits<<", attempts="<<cold.nativeCompilation.compilerAttempts<<", hits="<<cold.nativeCompilation.cacheHits<<", failed="<<cold.nativeCompilation.failedPreparations<<'\n';
    expect(cold.ok(),"cold native batch solves");expect(cold.nativeCompilation.translationUnits==2,"two required translation units");
    expect(cold.nativeCompilation.compilerAttempts==2,"two actual cold GCC invocations");
    expect(cold.nativeCompilation.effectiveJobs==2,"requested two compilation jobs");
    expect(cold.nativeCompilation.peakCompilerJobs==2,"GCC compilation intervals overlap");
    expect(cold.nativeCompilation.failedPreparations==0,"no silent preparation failure");
    expect(cold.nativeCompilation.compilationWallSeconds>0,"compiler wall interval recorded");
    expect(cold.output.size()>=2&&cold.output[0]=="0 0","first solver uses retain cold-cache flags");
    const auto warm=runtime::execute(source,{},options);if(!warm.ok())diagnose(warm);
    std::cerr<<"warm compilation: units="<<warm.nativeCompilation.translationUnits<<", attempts="<<warm.nativeCompilation.compilerAttempts<<", hits="<<warm.nativeCompilation.cacheHits<<'\n';
    expect(warm.ok(),"warm native batch solves");expect(warm.nativeCompilation.compilerAttempts==0,"warm run starts no compiler");
    expect(warm.nativeCompilation.cacheHits==2,"both prepared units reuse their caches");
    expect(warm.output.size()>=2&&warm.output[0]=="1 1","warm solver flags");
    auto parameterSource=source;parameterSource.replace(parameterSource.find("a=2.0"),5,"a=2.5");
    const auto parameters=runtime::execute(parameterSource,{},options);
    expect(parameters.ok(),"changed runtime parameters solve");expect(parameters.nativeCompilation.compilerAttempts==0,"parameter-only edit does not compile");
    const auto whitespace=runtime::execute("% comments and source offsets do not change structure\n\n"+source,{},options);
    expect(whitespace.ok(),"comment-only edit solves");expect(whitespace.nativeCompilation.compilerAttempts==0,"comment-only edit does not compile");
    auto bodySource=source;bodySource.replace(bodySource.find("x-3.0"),5,"x-3.1");
    const auto changed=runtime::execute(bodySource,{},options);if(!changed.ok())diagnose(changed);
    expect(changed.ok(),"changed native body solves");expect(changed.nativeCompilation.compilerAttempts==1,"only changed body recompiles");expect(changed.nativeCompilation.cacheHits==1,"unchanged body remains cached");
    const auto help=runtime::execute("help plot;help odesolve;help if;help(\"minimize\");lookfor sensitivity;help topics;");
    if(!help.ok())diagnose(help);
    expect(help.ok(),"MATLAB-style help and search parse and run");
    expect(help.output.size()==6,"one complete help response per command");
    expect(help.output.size()>4&&help.output[4].find("odeevents")!=std::string::npos,"search discovers sensitivity topic");
    const auto warnings=runtime::execute("warning(\"warning\");display(\"Warning: ordinary\");");
    expect(warnings.ok()&&warnings.warningOutputLines==std::vector<std::size_t>{0},"warning severity is explicit metadata");
    const auto cleared=runtime::execute("warning(\"old\");clc;display(1);");
    expect(cleared.ok()&&cleared.warningOutputLines.empty(),"clc clears warning-format metadata");
    auto invalid=options;invalid.nativeCompileJobs=65;expect(!runtime::execute(source,{},invalid).ok(),"invalid compiler job limit rejected");
    runtime::InterpreterSession session;auto sessionOptions=options;sessionOptions.dataDirectory=root/"session";sessionOptions.odeCacheDirectory=sessionOptions.dataDirectory/"odes";
    const auto definitions=session.submit("native function real prior(real x){return x+1.0;}native function real later(real x){return x-4.0;}",{},sessionOptions);
    expect(definitions.ok(),"native definitions persist in console workspace");
    const auto sessionSolve=session.submit("record a=root(prior,[-2.0,0.0]);record b=root(later,[0.0,5.0]);assert(a.converged&&b.converged);",{},sessionOptions);
    if(!sessionSolve.ok())diagnose(sessionSolve);
    expect(sessionSolve.ok(),"previous-command native callbacks solve");
    expect(sessionSolve.nativeCompilation.translationUnits==2&&sessionSolve.nativeCompilation.compilerAttempts==2,"workspace full definitions prepare two real callback bodies");
    language::AnalysisOptions modules;modules.moduleResolver=[](const std::string& name,const std::filesystem::path&)->std::optional<language::ModuleSource>{
        if(name=="a.p")return language::ModuleSource{"memory-a.p","native function real equation(real x){return x-1.0;}"};
        if(name=="b.p")return language::ModuleSource{"memory-b.p","native function real equation(real x){return x-2.0;}"};
        return {};
    };
    auto importOptions=options;importOptions.dataDirectory=root/"imports";importOptions.odeCacheDirectory=importOptions.dataDirectory/"odes";
    const auto imported=runtime::execute("import \"a.p\" as left;import \"b.p\" as right;callback left_alias=left.equation;record a=root(left_alias,[0.0,3.0]);record b=root(right.equation,[0.0,3.0]);assert(abs(a.x-1.0)<1e-12&&abs(b.x-2.0)<1e-12);",modules,importOptions);
    if(!imported.ok())diagnose(imported);
    expect(imported.ok(),"qualified imports and alias callback preserve distinct bodies");
    expect(imported.nativeCompilation.translationUnits==2&&imported.nativeCompilation.compilerAttempts==2,"same-named module functions are resolved in their own namespaces");
    auto experimentOptions=options;experimentOptions.dataDirectory=root/"experiments";experimentOptions.odeCacheDirectory=experimentOptions.dataDirectory/"odes";
    const std::string experimentSource="ode first_node(t,real x;real rate){x'=-rate*x;}ode second_node(t,real x){x'=-2*x;}record a=odeensemble(first_node,[1],options(parameters=options(rate=1.0),t1=.1,sample_step=.1));record b=odeensemble(second_node,[1],options(t1=.1,sample_step=.1));assert(a.runs==1&&b.runs==1);";
    const auto experiments=runtime::execute(experimentSource,{},experimentOptions);if(!experiments.ok())diagnose(experiments);
    expect(experiments.ok(),"native experiment consumers prepare actual model kernels");
    expect(experiments.nativeCompilation.translationUnits==2&&experiments.nativeCompilation.compilerAttempts==2,"independent experiment equations join one native compilation batch");
    expect(experiments.nativeCompilation.peakCompilerJobs==2&&experiments.nativeCompilation.failedPreparations==0,"experiment GCC jobs overlap without speculative callback binaries");
    auto runtimeEdit=experimentSource;runtimeEdit.replace(runtimeEdit.find("rate=1.0"),8,"rate=3.0");
    const auto reusedExperiments=runtime::execute(runtimeEdit,{},experimentOptions);if(!reusedExperiments.ok())diagnose(reusedExperiments);
    expect(reusedExperiments.ok()&&reusedExperiments.nativeCompilation.compilerAttempts==0,"experiment runtime parameter edits reuse both native model binaries");
    std::cout<<checks<<" native compilation/help checks, "<<failed<<" failures\n";
    return failed?1:0;
}
