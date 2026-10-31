#include "runtime/interpreter.hpp"
#include "evolution/evolution_session.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>
namespace {
int checks=0;void check(bool okay,const char* message){++checks;if(!okay)throw std::runtime_error(message);}
void run(const std::string& source,const penta::runtime::ExecutionOptions& options,bool success=true){++checks;const auto result=penta::runtime::execute(source,{},options);if(result.ok()==success)return;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<" near "<<source.substr(d.span.offset,90)<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<" near "<<source.substr(d.span.offset,90)<<'\n';throw std::runtime_error("Evolution workflow language fixture failed.");}
int bowl(void*,const double* x,uint64_t,double* f,uint64_t,char*,uint64_t){std::this_thread::sleep_for(std::chrono::microseconds(30));f[0]=(x[0]-.25)*(x[0]-.25)+(x[1]+.5)*(x[1]+.5);return 0;}
}
int main(int argc,char**argv){penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-evolution-session-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/"native-cache";options.maxOperations=100000000;if(argc>1)options.numericModuleDirectory=std::filesystem::absolute(argv[1]);try{
    std::vector<double> x{0,0},lower{-1,-1},upper{1,1};penta_evolution_request_v1 q{};q.struct_size=sizeof(q);q.algorithm=3;q.seed=42;q.population=24;q.dimension=2;q.objectives=1;q.max_generations=8;q.max_evaluations=10000;q.history_capacity=20;q.initial=x.data();q.lower=lower.data();q.upper=upper.data();q.maximum_wall_seconds=30;q.evaluate=bowl;penta::evolution::SessionOptions settings;settings.islands=2;settings.migrationInterval=2;settings.checkpoint=options.dataDirectory/"kernel-session.json";settings.modelIdentity="independent-real-bowl";const auto full=penta::evolution::SessionModule(options.numericModuleDirectory).run(q,settings,4);check(full.evolution.statistics.threadsUsed>1&&full.evolution.statistics.threadsUsed<=4,"Session islands/batch share the CPU budget.");check(std::filesystem::exists(settings.checkpoint)&&full.checkpointOkay,"Atomic session checkpoint was written.");settings.resume=true;const auto resumed=penta::evolution::SessionModule(options.numericModuleDirectory).run(q,settings,1);check(resumed.restoredGeneration==8&&resumed.evolution.populationX==full.evolution.populationX,"Resume retains state while changing only worker count.");
    std::ifstream in(settings.checkpoint,std::ios::binary);const std::string saved((std::istreambuf_iterator<char>(in)),{});settings.modelIdentity="different-native-model";bool rejected=false;try{(void)penta::evolution::SessionModule(options.numericModuleDirectory).run(q,settings);}catch(const std::exception&){rejected=true;}check(rejected,"Changed scientific identity rejected.");std::ifstream after(settings.checkpoint,std::ios::binary);check(std::string((std::istreambuf_iterator<char>(after)),{})==saved,"Incompatible checkpoint was preserved byte for byte.");
    run(R"(
native function real bowl(real x,real y){return (x-2)^2+(y+1)^2;}
native function real boundary(real x,real y){return x+y-0.5;}
native function real accurate(real x,real y){return (x-2)^2+(y+1)^2+0.01;}
record result=evolve(bowl,[0,0],options(lower=-4.0,upper=4.0,population=24,max_generations=25,restarts=0,islands=2,migration_interval=3,integer_coordinates=[1],inequality=boundary,seed=42,local_refinement=options(starts=4,separation=0.15),verification=options(objective=accurate,tolerance=1e-10,finalists=3)));
assert(result.feasible);assert(result.x(1)==round(result.x(1)));assert(abs(result.x(1)-2)<1e-8);assert(abs(result.x(2)+1.5)<1e-5);assert(abs(result.objective-0.25)<1e-5);assert(result.refinement.attempts>=1);assert(result.verification.any_feasible);assert(result.verification.finalists>0);assert(result.verification.best_objective>0.25);assert(result.verification.best_objective<0.261);
)",options);
    run(R"(
native function real wells(real x,real y){return (x*x-1)^2+0.2*(y-0.5)^2;}record multiple=evolve(wells,[0,0],options(lower=-2.0,upper=2.0,population=48,max_generations=2,restarts=0,islands=2,seed=7,local_refinement=options(starts=4,separation=0.1)));assert(multiple.refinement.attempts>=2);assert(multiple.objective<1e-8);
)",options);
    run(R"(
function integer stop_after_five(matrix x,record info=iterationinfo(),string state="iter"){return state=="iter"&&info.iteration>=5;}
native function real objective(real x,real y,real target){return (x-target)^2+(y+0.5)^2;}
record paused=evolve(objective,[0,0],options(parameters=options(target=0.25),lower=-1.0,upper=1.0,population=20,max_generations=12,restarts=0,checkpoint="language-session.json",islands=2,threads=4,OutputFcn=stop_after_five));assert(paused.generations==5);assert(paused.checkpoint_ok);
record resumed=evolve(objective,[0,0],options(parameters=options(target=0.25),lower=-1.0,upper=1.0,population=20,max_generations=12,restarts=0,checkpoint="language-session.json",islands=2,threads=1,resume=1));assert(resumed.restored_generation==5);assert(resumed.generations==12);
record uninterrupted=evolve(objective,[0,0],options(parameters=options(target=0.25),lower=-1.0,upper=1.0,population=20,max_generations=12,restarts=0,islands=2,threads=2));assert(norm(resumed.population_x-uninterrupted.population_x,"fro")==0);assert(resumed.cache_hit);assert(resumed.cache_key==uninterrupted.cache_key);
)",options);
    run(R"(native function real objective(real x){return x*x;}record result=evolve(objective,0.5,options(lower=0.0,upper=1.0,integer_coordinates=[1],islands=1));)",options,false);
    run(R"(native function real objective(real x){return x*x;}record result=evolve(objective,0.0,options(lower=-1.0,upper=1.0,islands=1,verification=options(objective=objective,tolerance=1.0)));)",options,false);
    std::cout<<"PASS "<<checks<<" native evolution workflow checks\n";
}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);}
