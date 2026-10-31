#include "runtime/interpreter.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
namespace {int checks=0;void run(penta::runtime::InterpreterSession& session,const std::string& source,const penta::runtime::ExecutionOptions& options,bool file=false,bool success=true,const penta::language::AnalysisOptions& analysis={}){++checks;const auto result=file?session.runFile(source,analysis,options):session.submit(source,analysis,options);if(result.ok()==success)return;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<" near "<<source.substr(d.span.offset,100)<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<" near "<<source.substr(d.span.offset,100)<<'\n';throw std::runtime_error("Research archive language fixture failed.");}}
int main(int argc,char**argv){using namespace penta::runtime;ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-archive-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/"kernels";options.maxOperations=100000000;if(argc>1)options.numericModuleDirectory=std::filesystem::absolute(argv[1]);try{
    InterpreterSession session;run(session,"rng(123);matrix discarded=rand(1,5);",options);std::mt19937_64 engine(123);for(unsigned i=0;i<5;++i)(void)engine();std::ostringstream expected;expected<<std::setprecision(17);for(unsigned i=0;i<3;++i){if(i)expected<<",";expected<<static_cast<double>(engine()>>11)*0x1.0p-53;}
    const auto script=std::string(R"(
matrix random_sample=rand(1,3);
assert(norm(random_sample-[)")+expected.str()+R"(])<1e-15);
native function real bowl(real x){return (x-2)^2;}
record fitted=minimize(bowl,0.0);
record data=options(format="fit",status="completed",cost=fitted.objective,samples=random_sample);
figure(61,"Name","Captured scientific plot");plot([0,1,2],[0,1,4]);xlabel("t");ylabel("square");
savearchive("random-fit.archive.json",data,options(name="Native random fit",workspace=options(samples=random_sample),overwrite=1));
record info=archiveinfo("random-fit.archive.json");assert(info.history_complete);assert(info.calls>=1);assert(info.invocations==1);
record loaded=loadarchive("random-fit.archive.json",data);assert(loaded.samples==random_sample);
record replayed=replayexperiment("random-fit.archive.json",options(output_directory="isolated-replay"));assert(replayed.ok);assert(replayed.invocations==1);
record catalogue=experimentcatalog(".",options(family="fit",scalar_path="cost",maximum=0.001));assert(catalogue.count==1);record entry=catalogueentry(catalogue,1);assert(entry.name=="Native random fit");
)";run(session,script,options,true);
    InterpreterSession console;run(console,"native function real square(real x){return x*x;}real a=3;",options);run(console,"real b=square(a);record calculation=options(format=\"native-calculation\",status=\"completed\",value=b);",options);run(console,"savearchive(\"console.archive.json\",calculation,options(name=\"Console study\",overwrite=1));",options);run(console,"record replayed=replayexperiment(\"console.archive.json\",options(output_directory=\"console-replay\"));assert(replayed.invocations==3);assert(b==9);",options);
    run(console,"record replayed=replayexperiment(\"console.archive.json\");",options,false,false);
    InterpreterSession budget;run(budget,"integer total=0;for(n=1:100){total=total+1;}record data=options(value=total);",options);run(budget,"for(n=1:100){total=total+1;}",options);run(budget,"savearchive(\"budget.archive.json\",data,options(overwrite=1));",options);
    run(budget,"record bounded=replayexperiment(\"budget.archive.json\",options(output_directory=\"budget-replay\",max_operations=2000));",options,false,false);
    run(budget,"record bounded=replayexperiment(\"budget.archive.json\",options(output_directory=\"budget-replay\",max_operations=3000));assert(bounded.operations<=3000);assert(bounded.invocations==3);",options);
    penta::language::AnalysisOptions imported;imported.sourceDirectory=options.dataDirectory;imported.moduleResolver=[&](const std::string&,const std::filesystem::path&)->std::optional<penta::language::ModuleSource>{return penta::language::ModuleSource{(options.dataDirectory/"captured-model.p").string(),"function real cube(real x){return x*x*x;}"};};InterpreterSession imports;run(imports,"import \"captured-model.p\" as model;real answer=model.cube(3);assert(answer==27);record data=options(value=answer);savearchive(\"imports.archive.json\",data,options(overwrite=1));",options,true,true,imported);run(imports,"record replayed=replayexperiment(\"imports.archive.json\",options(output_directory=\"imports-replay\"));assert(replayed.ok);",options);
    InterpreterSession algebras;run(algebras,"algebra H=builtin_algebra(\"quaternion\");element<H> q=element(H,1,2,3,4);record data=options(q=q);savearchive(\"quaternion.archive.json\",data,options(overwrite=1));record loaded=loadarchive(\"quaternion.archive.json\",data);assert(algebra_coefficients(loaded.q)==algebra_coefficients(q));",options,true);
    InterpreterSession gammaRecords;run(gammaRecords,"use_algebra(\"A_gamma\",gamma=1);record data=options(array=pentarray([i],[1]));savearchive(\"nd-gamma.archive.json\",data,options(overwrite=1));record info=archiveinfo(\"nd-gamma.archive.json\");assert(info.gamma_bound);",options,true);
    run(gammaRecords,"use_algebra(\"A_gamma\",gamma=0.5);record data=options(array=pentarray([i],[1]));record loaded=loadarchive(\"nd-gamma.archive.json\",data,options(allow_real_context=1));",options,true,false);
    std::cout<<"PASS "<<checks<<" research archive language checks\n";
}catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);}
