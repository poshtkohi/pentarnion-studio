#include "pde_experiment_api.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace {
void message(char* out,uint64_t cap,const char* text){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
struct Experiment{
    const penta_pde_experiment_host_v1& host;const penta_pde_experiment_request_v1& request;penta_pde_experiment_result_v1& result;std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    static int tasks(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity)noexcept{
        auto& e=*static_cast<Experiment*>(raw);try{for(uint64_t row=begin;row<end;++row){if(e.result.status[row]!=PENTA_PDE_RUN_PENDING)continue;if(e.host.cancelled&&e.host.cancelled(e.host.context))return PENTA_NUMERIC_CANCELLED;if(std::chrono::duration<double>(std::chrono::steady_clock::now()-e.started).count()>e.request.max_wall_seconds)return PENTA_NUMERIC_OK;
                penta_pde_run_v1 run{};run.struct_size=sizeof(run);struct Release{const penta_pde_experiment_host_v1& host;penta_pde_run_v1& run;~Release(){if(run.handle)host.release(host.context,&run);}}release{e.host,run};char detail[512]{};const auto status=e.host.solve(e.host.context,row,&run,detail,sizeof(detail));
                if(status){message(e.result.messages+row*512,512,detail[0]?detail:"Native PDE experiment failed.");if(status==PENTA_NUMERIC_CANCELLED)return status;e.result.status[row]=status==7?PENTA_PDE_RUN_TIMEOUT:PENTA_PDE_RUN_FAILED;continue;}
                const uint64_t n=e.request.nodes,c=e.request.components;if(run.struct_size<sizeof(run)||!run.samples||run.samples>1000000||run.samples*n*c>8000000||run.nodes!=n||run.components!=c||!run.times||!run.values||!run.means||!run.rms)throw std::runtime_error("PDE experiment returned an invalid retained field.");
                bool finite=std::isfinite(run.times[run.samples-1]);for(uint64_t j=0;j<n*c;++j)finite=finite&&std::isfinite(run.values[(run.samples-1)*n*c+j]);for(uint64_t j=0;j<c;++j)finite=finite&&std::isfinite(run.means[(run.samples-1)*c+j]);for(uint64_t j=0;j<run.samples;++j)finite=finite&&std::isfinite(run.rms[j])&&run.rms[j]>=0;
                if(!finite){e.result.status[row]=PENTA_PDE_RUN_FAILED;message(e.result.messages+row*512,512,"PDE final field/spatial diagnostics are nonfinite.");continue;}
                std::copy_n(run.values+(run.samples-1)*n*c,n*c,e.result.final_values+row*n*c);std::copy_n(run.means+(run.samples-1)*c,c,e.result.final_means+row*c);e.result.final_rms[row]=run.rms[run.samples-1];e.result.maximum_rms[row]=*std::max_element(run.rms,run.rms+run.samples);e.result.stop_times[row]=run.times[run.samples-1];e.result.steps[row]=run.steps;e.result.evaluations[row]=run.reaction_evaluations;e.result.linear_solves[row]=run.linear_solves;e.result.factorizations[row]=run.factorizations;e.result.inner_threads[row]=run.threads_used;e.result.status[row]=PENTA_PDE_RUN_COMPLETED;
            }return 0;}catch(const std::exception& failure){message(error,capacity,failure.what());return PENTA_NUMERIC_INTERNAL;}catch(...){message(error,capacity,"Unknown PDE experiment task failure.");return PENTA_NUMERIC_INTERNAL;}
    }
};
int run(const penta_pde_experiment_host_v1* host,const penta_pde_experiment_request_v1* request,penta_pde_experiment_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_PDE_EXPERIMENT_ABI_V1||!host->solve||!host->release||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw std::runtime_error("Incompatible PDE experiment ABI.");
        const auto& r=*request;auto& out=*result;if(!r.runs||r.runs>4096||r.nodes<3||r.nodes>16384||(r.components!=1&&r.components!=5)||r.runs*r.nodes*r.components>8000000||!r.batch_size||r.batch_size>256||!std::isfinite(r.max_wall_seconds)||r.max_wall_seconds<=0||out.capacity<r.runs||!out.status||!out.final_values||!out.final_means||!out.final_rms||!out.maximum_rms||!out.stop_times||!out.steps||!out.evaluations||!out.linear_solves||!out.factorizations||!out.inner_threads||!out.messages)throw std::runtime_error("Invalid PDE experiment dimensions or resource settings.");
        for(uint64_t row=0;row<r.runs;++row)if(out.status[row]>PENTA_PDE_RUN_TIMEOUT)throw std::runtime_error("Invalid resumed PDE experiment status.");
        Experiment e{*host,r,out};out.cancelled=0;
        auto checkpoint=[&]{out.completed=0;for(uint64_t row=0;row<r.runs;++row)if(out.status[row]!=PENTA_PDE_RUN_PENDING)++out.completed;out.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-e.started).count();if(host->checkpoint&&host->checkpoint(host->context,&out,error,capacity))throw std::runtime_error(error&&error[0]?error:"PDE checkpoint failed.");};checkpoint();
        for(uint64_t offset=0;offset<r.runs;offset+=r.batch_size){if(host->cancelled&&host->cancelled(host->context)){out.cancelled=1;break;}if(std::chrono::duration<double>(std::chrono::steady_clock::now()-e.started).count()>r.max_wall_seconds)break;const auto end=std::min(r.runs,offset+r.batch_size);int status=0;
            if(host->parallel_for){struct Batch{Experiment& e;uint64_t offset;static int task(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity)noexcept{auto& b=*static_cast<Batch*>(raw);return Experiment::tasks(&b.e,b.offset+begin,b.offset+end,error,capacity);}}batch{e,offset};status=host->parallel_for(host->context,end-offset,16384,Batch::task,&batch,error,capacity);}else status=Experiment::tasks(&e,offset,end,error,capacity);
            if(status==PENTA_NUMERIC_CANCELLED)out.cancelled=1;
            checkpoint();if(status&&status!=PENTA_NUMERIC_CANCELLED)throw std::runtime_error(error&&error[0]?error:"PDE experiment workers failed.");if(out.cancelled)break;
        }checkpoint();return 0;
    }catch(const std::exception& failure){message(error,capacity,failure.what());return 1;}catch(...){message(error,capacity,"Unknown PDE experiment failure.");return 4;}
}
const penta_pde_experiment_api_v1 api{sizeof(api),PENTA_PDE_EXPERIMENT_ABI_V1,run};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_pde_experiment_api_v1*penta_pde_experiment_query_v1(uint32_t version){return version==PENTA_PDE_EXPERIMENT_ABI_V1?&api:nullptr;}
