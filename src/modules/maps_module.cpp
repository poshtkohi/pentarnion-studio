#define PENTA_MAPS_MODULE_BUILD
#include "maps_api.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Clock=std::chrono::steady_clock;
void message(char* output,uint64_t capacity,const std::string& text){if(output&&capacity){const auto n=std::min<uint64_t>(text.size(),capacity-1);std::memcpy(output,text.data(),static_cast<std::size_t>(n));output[n]=0;}}
struct Failure:std::runtime_error {int code;Failure(int status,const std::string& text):std::runtime_error(text),code(status){}};
void require(bool valid,const char* text){if(!valid)throw Failure(PENTA_MAPS_ARGUMENT,text);}
uint64_t sampleCapacity(const penta_maps_request_v1& r){return (r.iterations-r.transient)/r.stride+1+((r.iterations-r.transient)%r.stride?1:0);}
int iterate(const penta_maps_host_v1* host,const penta_maps_request_v1* request,penta_maps_result_v1* result,char* error,uint64_t capacity)noexcept{
    try {
        require(host&&request&&result,"Map host/request/result is null.");
        require(host->struct_size>=sizeof(*host)&&host->abi_version==PENTA_MAPS_ABI_V1&&request->struct_size>=sizeof(*request)&&result->struct_size>=sizeof(*result),"Map ABI structure/version mismatch.");
        const auto& r=*request;auto& out=*result;
        require(r.dimension>=1&&r.dimension<=128,"Map dimension must be 1..128 real coordinates.");
        require(r.iterations<=1000000&&r.transient<=r.iterations&&r.stride>=1&&r.stride<=1000000,"Map iterations/transient/stride are invalid or exceed the one-million-step bound.");
        require(r.initial&&r.evaluate&&r.max_evaluations>=1,"Map input/callback/evaluation budget is invalid.");
        require(std::isfinite(r.maximum_wall_seconds)&&r.maximum_wall_seconds>0&&std::isfinite(r.escape_radius)&&r.escape_radius>=0,"Map wall budget must be finite/positive; escape radius finite/nonnegative.");
        const auto samples=sampleCapacity(r);
        require(samples<=1000000&&r.dimension<=8000000/samples,"Map retained sample/coordinate budget exceeded.");
        require(out.solution&&out.solution_capacity>=r.dimension&&out.states&&out.states_capacity>=samples*r.dimension&&out.indices&&out.indices_capacity>=samples,"Map output capacity is too small.");
        require(!r.final_jacobian||(out.jacobian&&out.jacobian_capacity>=r.dimension*r.dimension),"Map Jacobian output capacity is too small.");
        std::vector<double> x(r.initial,r.initial+r.dimension),next(r.dimension);
        for(const auto value:x)require(std::isfinite(value),"Map initial state contains a nonfinite coordinate.");
        out.escaped=0;out.iterations=0;out.evaluations=0;out.jacobian_evaluations=0;out.samples=0;out.escape_iteration=0;
        const auto started=Clock::now();
        auto stop=[&](uint64_t work=1){
            if(host->cancelled&&host->cancelled(host->context))throw Failure(PENTA_MAPS_CANCELLED,"Map iteration cancelled.");
            if(std::chrono::duration<double>(Clock::now()-started).count()>r.maximum_wall_seconds)throw Failure(PENTA_MAPS_BUDGET,"Map iteration exceeded max_wall_seconds.");
            if(host->charge){char text[1024]{};if(host->charge(host->context,work,text,sizeof(text)))throw Failure(PENTA_MAPS_BUDGET,text[0]?text:"Map host operation budget exhausted.");}
        };
        auto escaped=[&]{if(r.escape_radius==0)return false;double norm=0;for(const auto value:x)norm=std::hypot(norm,value);return norm>r.escape_radius;};
        auto capture=[&](uint64_t index,bool terminal=false){if(index<r.transient||((index-r.transient)%r.stride&&index!=r.iterations&&!terminal))return;const auto offset=out.samples*r.dimension;std::copy(x.begin(),x.end(),out.states+offset);out.indices[out.samples]=static_cast<double>(index);++out.samples;};
        stop();capture(0);
        if(escaped()){out.escaped=1;out.escape_iteration=0;}
        while(!out.escaped&&out.iterations<r.iterations){
            stop(r.dimension);char text[1024]{};
            if(out.evaluations>=r.max_evaluations)throw Failure(PENTA_MAPS_BUDGET,"Map exhausted max_evaluations.");
            ++out.evaluations;
            if(r.evaluate(r.callback_context,x.data(),r.dimension,next.data(),nullptr,text,sizeof(text)))throw Failure(PENTA_MAPS_CALLBACK,text[0]?text:"Native map callback failed.");
            for(const auto value:next)if(!std::isfinite(value))throw Failure(PENTA_MAPS_DOMAIN,"Native map returned a nonfinite coordinate.");
            x.swap(next);++out.iterations;
            if(escaped()){out.escaped=1;out.escape_iteration=out.iterations;}
            capture(out.iterations,out.escaped!=0);
        }
        std::copy(x.begin(),x.end(),out.solution);
        if(r.final_jacobian&&!out.escaped){
            stop(r.dimension*r.dimension);char text[1024]{};
            if(out.evaluations>=r.max_evaluations)throw Failure(PENTA_MAPS_BUDGET,"Map exhausted max_evaluations before its final Jacobian.");
            ++out.evaluations;++out.jacobian_evaluations;
            if(r.evaluate(r.callback_context,x.data(),r.dimension,next.data(),out.jacobian,text,sizeof(text)))throw Failure(PENTA_MAPS_CALLBACK,text[0]?text:"Native map Jacobian callback failed.");
            for(uint64_t n=0;n<r.dimension*r.dimension;++n)if(!std::isfinite(out.jacobian[n]))throw Failure(PENTA_MAPS_DOMAIN,"Native map Jacobian contains a nonfinite coordinate.");
        }
        stop();message(out.status,sizeof(out.status),out.escaped?"escaped":"completed");
        message(out.message,sizeof(out.message),out.escaped?"The orbit exceeded the configured finite escape radius; this is not a proof of unboundedness.":"Requested native map iterations completed; no boundedness, periodicity or chaos classification is implied.");
        return PENTA_MAPS_OK;
    }catch(const Failure& failure){message(error,capacity,failure.what());return failure.code;}
    catch(const std::exception& failure){message(error,capacity,failure.what());return PENTA_MAPS_INTERNAL;}
    catch(...){message(error,capacity,"Unknown native map failure.");return PENTA_MAPS_INTERNAL;}
}
const penta_maps_api_v1 api{sizeof(penta_maps_api_v1),PENTA_MAPS_ABI_V1,"penta_maps","native-discrete-maps-v1",1,iterate};
}
extern "C" PENTA_MAPS_EXPORT const penta_maps_api_v1* penta_maps_query_v1(uint32_t version){return version==PENTA_MAPS_ABI_V1?&api:nullptr;}
