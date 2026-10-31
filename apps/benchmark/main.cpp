#include "../cli_support.hpp"
#include "runtime/interpreter.hpp"
#include "runtime/data_io.hpp"
#include "ode/native_internal.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif
namespace {
using Clock=std::chrono::steady_clock;
std::uint64_t cumulativePeakRss(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info{};info.cb=sizeof(info);return GetProcessMemoryInfo(GetCurrentProcess(),&info,sizeof(info))?info.PeakWorkingSetSize:0;
#else
    rusage usage{};return getrusage(RUSAGE_SELF,&usage)==0?static_cast<std::uint64_t>(usage.ru_maxrss)*1024:0;
#endif
}
double snapshot(const penta::runtime::ExecutionResult& result,const char* name){for(const auto& value:result.variables)if(value.name==name)return std::stod(value.value);throw std::runtime_error(std::string("Missing benchmark output: ")+name);}
std::string diagnostics(const penta::runtime::ExecutionResult& result){std::string text;for(const auto* list:{&result.analysis.diagnostics,&result.runtimeDiagnostics})for(const auto& item:*list)if(item.severity==penta::language::Severity::Error)text+=item.code+": "+item.message+"\n";return text;}
int run(const std::vector<std::string>& arguments){
    bool quick=false,overwrite=false;unsigned workers=std::min(4u,std::max(1u,std::thread::hardware_concurrency())),repetitions=3;std::filesystem::path output;
    for(std::size_t i=1;i<arguments.size();++i){const auto& arg=arguments[i];if(arg=="--help"){std::cout<<"penta-benchmark [--quick] [--threads 1..64] [--repetitions 1..10] [--output report.json] [--overwrite]\n";return 0;}if(arg=="--quick"){quick=true;continue;}if(arg=="--overwrite"){overwrite=true;continue;}if(i+1==arguments.size())throw std::runtime_error("Benchmark option needs a value.");const auto& value=arguments[++i];if(arg=="--output")output=penta::cli::utf8Path(value);else if(arg=="--threads"||arg=="--repetitions"){if(value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Benchmark counts require unsigned decimal integers.");const auto n=std::stoul(value);if(n<1||n>(arg=="--threads"?64u:10u))throw std::runtime_error("Benchmark count exceeds its fixed allowance.");if(arg=="--threads")workers=static_cast<unsigned>(n);else repetitions=static_cast<unsigned>(n);}else throw std::runtime_error("Unknown benchmark option: "+arg);}
    if(!output.empty()&&std::filesystem::exists(output)&&!overwrite)throw std::runtime_error("Benchmark report exists; select a new path or explicit --overwrite.");
    const auto started=Clock::now();nlohmann::json report{{"format","PentarnionControlledBenchmarks"},{"version",1},{"logical_cpus",std::thread::hardware_concurrency()},{"requested_parallel_workers",workers},{"repetitions",repetitions},{"quick",quick},{"memory_measurement","process cumulative peak resident bytes; includes prior cases and native modules"},{"timing","in-process repeated numerical work after one warm call; native compilation excluded by tic/toc; separate full-request wall time"},{"cases",nlohmann::json::array()}};
#ifdef _WIN32
    report["platform"]="windows";
#else
    report["platform"]="linux";
#endif
    penta::runtime::ExecutionOptions options;options.maxOperations=50000000;options.maxNativeOperations=10000000000ULL;options.maxMatrixElements=1000000;options.maxMatrixCoordinates=4000000;options.allowFigures=false;
    options.dataDirectory=std::filesystem::temp_directory_path()/("pentarnion-benchmark-"+std::to_string(Clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/"cache/odes";
    unsigned failures=0;
    for(const auto seed:{104729u,130363u,155921u})for(const auto threads:{1u,workers})for(const auto& family:{std::string("real"),std::string("pentarnion"),std::string("quaternion"),std::string("sedenion")}){
        const unsigned dimension=family=="real"?(quick?128:256):family=="pentarnion"?(quick?24:96):(quick?16:48);
        const auto shape=std::to_string(dimension)+","+std::to_string(dimension);std::string source="rng("+std::to_string(seed)+");threads("+std::to_string(threads)+");";
        if(family=="real")source+="matrix left=randn("+shape+");matrix right=randn("+shape+");";
        else if(family=="pentarnion")source+="pentarnionmatrix left=randn("+shape+")+i*randn("+shape+")+j*randn("+shape+");pentarnionmatrix right=randn("+shape+")+k*randn("+shape+")+ell*randn("+shape+");";
        else{const auto first=family=="quaternion"?"K.i":"K.e1",second=family=="quaternion"?"K.j":"K.e10";source+="algebra K=builtin_algebra(\""+family+"\");matrix<K,"+shape+"> left=randn("+shape+")+"+first+"*randn("+shape+");matrix<K,"+shape+"> right=randn("+shape+")+"+second+"*randn("+shape+");";}
        source+="threads(1);matrix reference=left*right;threads("+std::to_string(threads)+");matrix computed=left*right;tic;for(repetition_index=1:"+std::to_string(repetitions)+"){computed=left*right;}real elapsed_seconds=toc();";
        if(family=="real")source+="real validation_error=norm(computed-reference,\"fro\")/max(1.0,norm(reference,\"fro\"));";
        else{const auto coefficients=family=="pentarnion"?5:family=="quaternion"?4:16;source+="real error_squared=0;real reference_squared=0;for(coefficient_index=0:"+std::to_string(coefficients-1)+"){error_squared=error_squared+norm(coeff(computed-reference,coefficient_index),\"fro\")^2;reference_squared=reference_squared+norm(coeff(reference,coefficient_index),\"fro\")^2;}real validation_error=sqrt(error_squared)/max(1.0,sqrt(reference_squared));";}
        source+="assert(validation_error<1e-10);display(parallel_info());";
        // Typed results keep the actual entry algebra throughout the benchmark.
        const auto resultType=family=="real"?std::string("matrix"):family=="pentarnion"?"pentarnionmatrix":"matrix<K,"+shape+">";
        for(const auto name:{"reference","computed"}){const auto old=std::string("matrix ")+name+"=";const auto at=source.find(old);if(at!=std::string::npos)source.replace(at,old.size(),resultType+" "+name+"=");}
        const auto before=Clock::now();const auto result=penta::runtime::execute(source,{},options);nlohmann::json row{{"workload","ordered_matrix_product"},{"family",family},{"dimension",dimension},{"seed",seed},{"thread_maximum",threads},{"source_sha256",penta::ode::detail::sha256(source)},{"request_wall_seconds",std::chrono::duration<double>(Clock::now()-before).count()},{"cumulative_peak_rss_bytes",cumulativePeakRss()},{"passed",result.ok()}};
        if(result.ok()){row["execution_seconds"]=snapshot(result,"elapsed_seconds");row["relative_validation_error"]=snapshot(result,"validation_error");if(!result.output.empty())row["native_thread_report"]=result.output.back();}
        else{++failures;row["diagnostics"]=diagnostics(result);std::cerr<<family<<" seed"<<seed<<" threads"<<threads<<": "<<row["diagnostics"].get<std::string>();}
        report["cases"].push_back(std::move(row));
    }
    for(const auto seed:{104729u,130363u,155921u})for(const auto threads:{1u,workers})for(const auto& workload:{std::string("native_ode"),std::string("implicit_nonlinear_pde")}){
        const auto rate=std::to_string(1.0+double(seed%7u)/10.0);std::string source="threads("+std::to_string(threads)+");";
        if(workload=="native_ode"){
            source+="ode benchmark_decay(t,real y;real rate){y'=-rate*y;}solution simulation=odesolve(benchmark_decay,y=1,rate="+rate+",t0=0,t1=1,sample_step=.005,method=\"bdf\",jacobian=\"analytic\",rtol=1e-9,atol=1e-11);tic;for(repetition_index=1:"+std::to_string(repetitions)+"){simulation=odesolve(benchmark_decay,y=1,rate="+rate+",t0=0,t1=1,sample_step=.005,method=\"bdf\",jacobian=\"analytic\",rtol=1e-9,atol=1e-11);}real elapsed_seconds=toc();real validation_error=abs(simulation.y(end)-exp(-"+rate+"));assert(validation_error<1e-7);display(simulation);";
        }else{
            const auto nodes=quick?33:257;
            source+="native function real benchmark_reaction(real u,real t,real x,real rate){return -rate*u*u*u;}matrix grid_axis=linspace(0,1,"+std::to_string(nodes)+");record simulation=pdeimplicit(benchmark_reaction,grid_axis,1.0,options(parameters=options(rate="+rate+"),diffusion=0,t1=.1,time_step=.01,sample_step=.01,rtol=1e-4,atol=1e-8,threads="+std::to_string(threads)+"));tic;for(repetition_index=1:"+std::to_string(repetitions)+"){simulation=pdeimplicit(benchmark_reaction,grid_axis,1.0,options(parameters=options(rate="+rate+"),diffusion=0,t1=.1,time_step=.01,sample_step=.01,rtol=1e-4,atol=1e-8,threads="+std::to_string(threads)+"));}real elapsed_seconds=toc();matrix retained=pdeimplicitcoeff(simulation,0);real validation_error=abs(retained(end,1)-1/sqrt(1+.2*"+rate+"));assert(validation_error<.003);display(simulation);";
        }
        const auto before=Clock::now();const auto result=penta::runtime::execute(source,{},options);nlohmann::json row{{"workload",workload},{"family","real"},{"seed",seed},{"thread_maximum",threads},{"source_sha256",penta::ode::detail::sha256(source)},{"request_wall_seconds",std::chrono::duration<double>(Clock::now()-before).count()},{"cumulative_peak_rss_bytes",cumulativePeakRss()},{"passed",result.ok()}};
        if(result.ok()){row["execution_seconds"]=snapshot(result,"elapsed_seconds");row["absolute_validation_error"]=snapshot(result,"validation_error");if(!result.output.empty())row["native_report"]=result.output.back();}
        else{++failures;row["diagnostics"]=diagnostics(result);std::cerr<<workload<<" seed"<<seed<<": "<<row["diagnostics"].get<std::string>();}
        report["cases"].push_back(std::move(row));
    }
    report["failures"]=failures;report["total_wall_seconds"]=std::chrono::duration<double>(Clock::now()-started).count();report["performance_claim"]="Measurements describe this machine/workload only; no guaranteed speedup, platform equivalence or comparison with another product.";
    const auto encoded=report.dump(2)+"\n";if(output.empty())std::cout<<encoded;else penta::runtime::dataio::writeBytesAtomic(output,encoded);
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);return failures?1:0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){
#else
int main(int argc,char**argv){
#endif
    try{return run(penta::cli::arguments(argc,argv));}catch(const std::exception& e){std::cerr<<"penta-benchmark: "<<e.what()<<'\n';return 2;}
}
