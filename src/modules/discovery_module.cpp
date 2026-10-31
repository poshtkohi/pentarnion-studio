#include "discovery_api.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
using Clock=std::chrono::steady_clock;
void message(char* target,uint64_t capacity,const char* text){if(target&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(target,text,n);target[n]=0;}}
struct Batch {
    const penta_discovery_host_v1* host;
    const penta_discovery_request_v1* request;
    penta_discovery_result_v1* result;
    uint64_t offset;
    Clock::time_point start;
    static int task(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity)noexcept {
        auto& b=*static_cast<Batch*>(raw);const auto& q=*b.request;auto& out=*b.result;
        try{
            for(uint64_t local=begin;local<end;++local){
                const auto row=local+b.offset;
                if(out.status[row]!=PENTA_DISCOVERY_PENDING&&out.status[row]!=PENTA_DISCOVERY_CANCELLED)continue;
                if(b.host->cancelled&&b.host->cancelled(b.host->context))return PENTA_NUMERIC_CANCELLED;
                if(std::chrono::duration<double>(Clock::now()-b.start).count()>q.maximum_wall_seconds)return PENTA_NUMERIC_OK;
                penta_discovery_trajectory_v1 trajectory{};trajectory.struct_size=sizeof(trajectory);
                struct Release {const penta_discovery_host_v1* host;penta_discovery_trajectory_v1* trajectory;~Release(){if(trajectory->handle)host->release(host->context,trajectory);}} release{b.host,&trajectory};
                char error[512]{};
                const auto status=b.host->solve(b.host->context,row,q.initial+row*q.coordinates,
                    q.parameters?q.parameter_values+row*q.parameters:nullptr,q.gamma[row],&trajectory,error,sizeof(error));
                if(status){
                    out.status[row]=status==PENTA_NUMERIC_CANCELLED?PENTA_DISCOVERY_CANCELLED:
                        status==7?PENTA_DISCOVERY_TIMEOUT:PENTA_DISCOVERY_FAILURE;
                    message(out.messages+row*512,512,error[0]?error:"Native candidate failed.");
                    if(status==PENTA_NUMERIC_CANCELLED)return status;
                    continue;
                }
                if(trajectory.struct_size<sizeof(trajectory)||trajectory.coordinates!=q.coordinates||trajectory.samples<1||!trajectory.times||!trajectory.values||!trajectory.final_rhs)
                    throw std::runtime_error("Native discovery received an invalid trajectory layout.");
                const auto count=trajectory.samples,n=q.coordinates,p=q.physical_coordinates;
                const auto* last=trajectory.values+(count-1)*n;
                std::copy_n(last,n,out.states+row*n);out.stop_times[row]=trajectory.times[count-1];
                out.steps[row]=trajectory.steps;out.evaluations[row]=trajectory.rhs_evaluations;
                double maxNorm=0,rhs=0,scale=1;
                for(uint64_t k=0;k<p;++k){rhs=std::hypot(rhs,trajectory.final_rhs[k]);scale=std::hypot(scale,last[k]);}
                for(uint64_t sample=0;sample<count;++sample){double norm=0;for(uint64_t k=0;k<p;++k)norm=std::hypot(norm,trajectory.values[sample*n+k]);maxNorm=std::max(maxNorm,norm);}
                out.maximum_norm[row]=maxNorm;out.rhs_norm[row]=rhs;
                const double observed=std::abs(out.stop_times[row]-q.t0);
                double minimum=std::numeric_limits<double>::max(),period=0;std::vector<double> returns;
                for(uint64_t sample=0;sample+1<count;++sample){
                    const double age=std::abs(out.stop_times[row]-trajectory.times[sample]);
                    if(age<q.min_return_time||std::abs(trajectory.times[sample]-q.t0)<q.transient)continue;
                    double distance=0;for(uint64_t k=0;k<p;++k)distance=std::hypot(distance,last[k]-trajectory.values[sample*n+k]);
                    const double normalized=distance/scale;
                    if(normalized<minimum){minimum=normalized;period=age;}
                    if(normalized<=q.return_tolerance)returns.push_back(age);
                }
                out.return_error[row]=minimum==std::numeric_limits<double>::max()?-1:minimum;
                out.period[row]=0;
                if(trajectory.threshold_reached){out.status[row]=PENTA_DISCOVERY_THRESHOLD;}
                else if(rhs<=q.equilibrium_tolerance){
                    double spread=0;uint64_t first=count*3/4;
                    for(uint64_t sample=first;sample<count;++sample){double distance=0;for(uint64_t k=0;k<p;++k)distance=std::hypot(distance,last[k]-trajectory.values[sample*n+k]);spread=std::max(spread,distance/scale);}
                    out.status[row]=spread<=q.equilibrium_tolerance?PENTA_DISCOVERY_EQUILIBRIUM:PENTA_DISCOVERY_BOUNDED;
                }else if(minimum<=q.return_tolerance){
                    // Independent sample-grid repeats are candidates only. Use
                    // separated repeated returns, never a closed projection.
                    std::sort(returns.begin(),returns.end());std::vector<double> groups;
                    const double step=count>1?std::abs(trajectory.times[1]-trajectory.times[0]):observed;
                    for(double value:returns)if(groups.empty()||value-groups.back()>std::max(step*3,q.min_return_time*.25))groups.push_back(value);
                    bool periodic=groups.size()>=3;
                    if(periodic){const double base=groups.front();for(std::size_t i=1;i<3;++i)periodic=periodic&&std::abs(groups[i]-(i+1)*base)<=std::max(step*4,base*.03);if(periodic)out.period[row]=base;}
                    out.status[row]=periodic?PENTA_DISCOVERY_PERIODIC:PENTA_DISCOVERY_RECURRENT;
                }else out.status[row]=observed>q.transient?PENTA_DISCOVERY_BOUNDED:PENTA_DISCOVERY_INCONCLUSIVE;
                (void)period;
            }
            return PENTA_NUMERIC_OK;
        }catch(const std::exception& e){message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){message(error,capacity,"Native discovery worker failed.");return PENTA_NUMERIC_INTERNAL;}
    }
};
int run(const penta_discovery_host_v1* h,const penta_discovery_request_v1* q,penta_discovery_result_v1* out,char* error,uint64_t capacity)noexcept{
    try{
        if(!h||!q||!out||h->struct_size<sizeof(*h)||h->abi_version!=PENTA_DISCOVERY_ABI_V1||q->struct_size<sizeof(*q)||out->struct_size<sizeof(*out)||!h->solve||!h->release||!h->parallel_for)
            throw std::runtime_error("Incompatible discovery ABI.");
        if(q->runs<1||q->runs>100000||q->coordinates<1||q->coordinates>128||q->physical_coordinates<1||q->physical_coordinates>q->coordinates||q->parameters>128||out->capacity<q->runs||!q->initial||!q->gamma||(q->parameters&&!q->parameter_values)||!out->status||!out->states||!out->stop_times||!out->maximum_norm||!out->return_error||!out->period||!out->rhs_norm||!out->steps||!out->evaluations||!out->messages)
            throw std::runtime_error("Invalid discovery dimensions/buffers.");
        for(double v:{q->t0,q->t1,q->transient,q->return_tolerance,q->equilibrium_tolerance,q->min_return_time,q->maximum_wall_seconds})if(!std::isfinite(v))throw std::runtime_error("Discovery settings must be finite.");
        if(q->t1==q->t0||q->transient<0||q->transient>=std::abs(q->t1-q->t0)||q->return_tolerance<=0||q->equilibrium_tolerance<=0||q->min_return_time<=0||q->maximum_wall_seconds<=0||q->batch_size<1||q->batch_size>4096)throw std::runtime_error("Invalid discovery time/diagnostic/batch settings.");
        const auto started=Clock::now();int status=0;
        for(uint64_t offset=0;offset<q->runs;offset+=q->batch_size){
            Batch batch{h,q,out,offset,started};
            const auto count=std::min(q->batch_size,q->runs-offset);bool pending=false;
            for(uint64_t row=offset;row<offset+count;++row)pending=pending||out->status[row]==PENTA_DISCOVERY_PENDING||out->status[row]==PENTA_DISCOVERY_CANCELLED;
            if(pending)status=h->parallel_for(h->context,count,4096,Batch::task,&batch,error,capacity);
            out->completed=0;for(uint64_t row=0;row<q->runs;++row)out->completed+=out->status[row]!=PENTA_DISCOVERY_PENDING&&out->status[row]!=PENTA_DISCOVERY_CANCELLED;
            if(h->checkpoint&&h->checkpoint(h->context,out,error,capacity))return PENTA_NUMERIC_INTERNAL;
            if(status||std::chrono::duration<double>(Clock::now()-started).count()>q->maximum_wall_seconds)break;
        }
        out->elapsed_seconds=std::chrono::duration<double>(Clock::now()-started).count();return status;
    }catch(const std::exception& e){message(error,capacity,e.what());return PENTA_NUMERIC_ARGUMENT;}catch(...){message(error,capacity,"Unknown native discovery failure.");return PENTA_NUMERIC_INTERNAL;}
}
const penta_discovery_api_v1 api{sizeof(api),PENTA_DISCOVERY_ABI_V1,1,run};
}
#ifdef _WIN32
#define PENTA_DISCOVERY_EXPORT __declspec(dllexport)
#else
#define PENTA_DISCOVERY_EXPORT __attribute__((visibility("default")))
#endif
extern "C" PENTA_DISCOVERY_EXPORT const penta_discovery_api_v1* penta_discovery_query_v1(uint32_t abi){return abi==PENTA_DISCOVERY_ABI_V1?&api:nullptr;}
