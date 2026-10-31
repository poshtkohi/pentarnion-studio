#include "modules/evolution_session_api.h"
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
int checks=0;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);++checks;}
struct Context{std::string state;std::size_t stop=0;bool lattice=true;};
int parallel(void*,uint64_t count,uint64_t,penta_numeric_range_task_v1 fn,void* raw,char* error,uint64_t cap){return fn(raw,0,count,error,cap);}
int output(void* raw,const penta_iteration_snapshot_v1* state,char*,uint64_t){return static_cast<Context*>(raw)->stop&&state->phase==PENTA_ITERATION_ITER&&state->iteration>=static_cast<Context*>(raw)->stop;}
int snapshot(void* raw,const char* bytes,uint64_t length,char*,uint64_t){static_cast<Context*>(raw)->state.assign(bytes,length);return 0;}
int objective(void* raw,const double* x,uint64_t,double* f,uint64_t m,char*,uint64_t){if(raw)static_cast<Context*>(raw)->lattice=static_cast<Context*>(raw)->lattice&&x[0]==std::trunc(x[0]);f[0]=(x[0]-2)*(x[0]-2)+(x[1]+1)*(x[1]+1);if(m==2)f[1]=(x[0]+2)*(x[0]+2)+(x[1]-1)*(x[1]-1);return 0;}
int inequality(void*,const double* x,uint64_t,double* f,uint64_t,char*,uint64_t){f[0]=x[0]+x[1]-.5;return 0;}
struct Result{penta_evolution_result_v2 out{};std::vector<double>x,px,pf,frontx,frontf,history,constraints;std::string error;int status=0;};
Result run(const penta_evolution_api_v2& api,unsigned algorithm,Context& ctx,const std::string& restored={},bool integer=false,bool constrained=false,unsigned islands=2){
    const double initial[]{0,0},lower[]{-4,-4},upper[]{4,4};const uint8_t mask[]{1,0};
    penta_evolution_request_v1 q{};q.struct_size=sizeof(q);q.algorithm=algorithm;q.seed=7301;q.population=20;q.dimension=2;q.objectives=algorithm==5?2:1;q.max_generations=14;q.max_evaluations=10000;q.history_capacity=30;q.initial=initial;q.lower=lower;q.upper=upper;q.maximum_wall_seconds=30;q.evaluate=objective;q.callback_context=integer?&ctx:nullptr;
    Result r;r.x.resize(2);r.px.resize(40*2);r.pf.resize(40*q.objectives);r.frontx.resize(80);r.frontf.resize(40*q.objectives);r.history.resize(120);r.constraints.resize(40);r.out.struct_size=sizeof(r.out);auto& b=r.out.base;b.struct_size=sizeof(b);b.solution=r.x.data();b.solution_capacity=r.x.size();b.population_x=r.px.data();b.population_x_capacity=r.px.size();b.population_f=r.pf.data();b.population_f_capacity=r.pf.size();b.pareto_x=r.frontx.data();b.pareto_x_capacity=r.frontx.size();b.pareto_f=r.frontf.data();b.pareto_f_capacity=r.frontf.size();b.history=r.history.data();b.history_capacity=r.history.size();r.out.population_constraints=r.constraints.data();r.out.constraint_capacity=r.constraints.size();
    penta_evolution_host_v1 host{sizeof(host),PENTA_EVOLUTION_ABI_V1,&ctx,nullptr,nullptr,parallel,output};penta_evolution_session_v2 s{};s.struct_size=sizeof(s);s.abi_version=PENTA_EVOLUTION_ABI_V2;s.islands=islands;s.migration_interval=2;s.migrants=1;s.integer_coordinates=integer?mask:nullptr;s.constraint_tolerance=1e-7;s.inequality_count=constrained?1:0;s.inequality=constrained?inequality:nullptr;s.restored_state=restored.data();s.restored_bytes=restored.size();s.maximum_state_bytes=1024*1024;s.checkpoint_context=&ctx;s.checkpoint=snapshot;
    char error[2048]{};r.status=api.solve(&host,&q,&s,&r.out,error,sizeof(error));r.error=error;r.px.resize(b.population_rows*2);r.pf.resize(b.population_rows*q.objectives);r.history.resize(b.history_rows*4);return r;
}
}
int main(int argc,char**argv){try{check(argc==2,"Module directory argument required.");
#ifdef _WIN32
const auto module=LoadLibraryA((std::string(argv[1])+"/penta_evolution.dll").c_str());
#else
const auto module=dlopen((std::string(argv[1])+"/libpenta_evolution.so").c_str(),RTLD_NOW);
#endif
check(module,"Could not load private evolution DLL.");
#ifdef _WIN32
const auto address=GetProcAddress(module,"penta_evolution_query_v2");const penta_evolution_api_v2*(*query)(uint32_t)=nullptr;static_assert(sizeof(query)==sizeof(address));std::memcpy(&query,&address,sizeof(query));
#else
const auto query=reinterpret_cast<const penta_evolution_api_v2*(*)(uint32_t)>(dlsym(module,"penta_evolution_query_v2"));
#endif
check(query,"V2 query unavailable.");const auto* api=query(PENTA_EVOLUTION_ABI_V2);check(api&&api->solve,"V2 session API invalid.");for(unsigned algorithm=1;algorithm<=5;++algorithm){Context full;const auto a=run(*api,algorithm,full);if(a.status)throw std::runtime_error(a.error);Context interrupted;interrupted.stop=5;const auto p=run(*api,algorithm,interrupted);check(!p.status&&p.out.base.generations==5,"Pause did not stop at a complete generation.");const auto saved=interrupted.state;interrupted.stop=0;const auto b=run(*api,algorithm,interrupted,saved);if(b.status)throw std::runtime_error(b.error);check(b.out.restored_generation==5,"Restored generation identity changed.");check(a.x==b.x&&a.px==b.px&&a.pf==b.pf&&a.history==b.history&&a.out.base.evaluations==b.out.base.evaluations,"Resume changed algorithm population/adaptation/RNG trajectory.");if(full.state!=interrupted.state){std::ofstream("../frontier-native-temp/evolution-full.json")<<full.state;std::ofstream("../frontier-native-temp/evolution-resumed.json")<<interrupted.state;throw std::runtime_error("Final numeric checkpoint differs for algorithm "+std::to_string(algorithm));}check(true,"Exact numeric checkpoint.");check(a.out.migrations>0||algorithm==5,"Synchronous islands did not migrate any improving candidate.");}
    Context lattice;const auto mixed=run(*api,1,lattice,{},true,true);if(mixed.status)throw std::runtime_error(mixed.error);check(lattice.lattice,"Mixed-integer callback received a nonintegral coordinate.");check(mixed.out.feasible&&mixed.x[0]+mixed.x[1]<=.5000001,"Nonlinear constraints were lost in mixed-integer search.");
    auto forged=nlohmann::json::parse(lattice.state);forged["islands"][0]["adaptation"]["inner"]["F"]=nlohmann::json::array({1,2});Context bad;const auto invalid=run(*api,1,bad,forged.dump(),true,true);check(invalid.status!=0,"Corrupt adaptation dimensions were accepted.");check(bad.state.empty(),"Rejected state was overwritten.");
    Context cma;run(*api,3,cma);auto large=nlohmann::json::parse(cma.state);large["islands"][0]["adaptation"]["C"]["rows"]=1000000000000ull;Context attack;const auto rejected=run(*api,3,attack,large.dump());check(rejected.status!=0&&attack.state.empty(),"Forged allocation dimensions were not rejected before state use.");
    auto rng=nlohmann::json::parse(cma.state);rng["islands"][0]["random"]=std::string("0 18446744073709551615");Context broken;check(run(*api,3,broken,rng.dump()).status!=0,"Malformed fixed-size RNG state was accepted.");
    std::cout<<"PASS "<<checks<<" evolutionary session checks\n";
#ifdef _WIN32
FreeLibrary(module);
#else
dlclose(module);
#endif
return 0;
}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}}
