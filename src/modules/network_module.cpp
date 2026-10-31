#include "network_api.h"
#include "network_operator_api.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <span>
#include <string>
#include <vector>
#include <cvodes/cvodes.h>
#include <cvodes/cvodes_ls.h>
#include <nvector/nvector_serial.h>
#include <sunlinsol/sunlinsol_spgmr.h>
namespace {
using Clock=std::chrono::steady_clock;
struct Error:std::runtime_error{int status;Error(int value,const char* text):std::runtime_error(text),status(value){}};
void message(char* out,uint64_t cap,const char* text){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
struct Arc {uint64_t source;double weight;uint64_t edge;};
struct Network {
    const penta_network_host_v1& host;const penta_network_request_v1& r;penta_network_result_v1& result;Clock::time_point start=Clock::now();std::vector<uint64_t> pointers;std::vector<Arc> arcs;const double* operators=nullptr;uint64_t work=0;
    Network(const penta_network_host_v1& h,const penta_network_request_v1& q,penta_network_result_v1& out,const double* op=nullptr):host(h),r(q),result(out),operators(op){}
    long double coupling(const Arc& arc,uint64_t row,const double* other,const double* self)const{
        if(!operators)return static_cast<long double>(r.diffusion[row])*arc.weight*(static_cast<long double>(other[row])-self[row]);
        const auto* matrix=operators+arc.edge*r.coordinates*r.coordinates;long double value=0;
        for(uint64_t col=0;col<r.coordinates;++col)value+=static_cast<long double>(matrix[row*r.coordinates+col])*(static_cast<long double>(other[col])-self[col]);
        return static_cast<long double>(r.diffusion[row])*arc.weight*value;
    }
    long double diagonal(uint64_t node,uint64_t row,uint64_t col)const{
        if(!operators)return row==col?static_cast<long double>(r.diffusion[row])*bdfDegree[node]:0;
        long double value=0;for(auto at=pointers[node];at<pointers[node+1];++at){const auto& arc=arcs[at];value+=static_cast<long double>(arc.weight)*operators[(arc.edge*r.coordinates+row)*r.coordinates+col];}
        return r.diffusion[row]*value;
    }
    void check(uint64_t count=0){if(host.cancelled&&host.cancelled(host.context))throw Error(PENTA_NUMERIC_CANCELLED,"Network solve cancelled.");if(std::chrono::duration<double>(Clock::now()-start).count()>r.max_seconds)throw Error(7,"Network wall-time budget exceeded.");if(count>r.max_work-std::min(work,r.max_work))throw Error(7,"Network native work budget exceeded.");work+=count;if(host.charge&&count){char error[1024]{};if(host.charge(host.context,count,error,sizeof(error)))throw Error(7,error[0]?error:"Network host work quota reached.");}}
    void build(){
        pointers.assign(r.nodes+1,0);for(uint64_t e=0;e<r.edges;++e){check(1);const auto a=r.endpoints[2*e],b=r.endpoints[2*e+1];const auto w=r.weights[e];if(a<1||a>r.nodes||b<1||b>r.nodes||!std::isfinite(w)||(!r.general_coupling&&w<0))throw Error(PENTA_NUMERIC_ARGUMENT,"Diffusion requires valid endpoints and nonnegative real weights; signed weights require coupling=general.");if(a!=b){++pointers[b];if(!r.directed)++pointers[a];}}
        for(uint64_t n=0;n<r.nodes;++n)pointers[n+1]+=pointers[n];
        arcs.resize(pointers.back());auto cursor=pointers;for(uint64_t e=0;e<r.edges;++e){const auto a=r.endpoints[2*e]-1,b=r.endpoints[2*e+1]-1;if(a!=b){arcs[cursor[b]++]={a,r.weights[e],e};if(!r.directed)arcs[cursor[a]++]={b,r.weights[e],e};}}
    }
    struct Evaluation {
        Network& network;double time;const std::vector<double>& state;std::vector<double>& output;
        static int run(void* raw,uint64_t first,uint64_t last,char* error,uint64_t cap)noexcept{
            auto& e=*static_cast<Evaluation*>(raw);const auto& q=e.network.r;const auto& h=e.network.host;
            try{for(uint64_t node=first;node<last;++node){if(h.cancelled&&h.cancelled(h.context))return PENTA_NUMERIC_CANCELLED;if((node&15)==0&&std::chrono::duration<double>(Clock::now()-e.network.start).count()>q.max_seconds){message(error,cap,"Network wall-time budget exceeded inside a native node batch.");return 7;}auto* out=e.output.data()+node*q.coordinates;const auto* self=e.state.data()+node*q.coordinates;const int status=h.local(h.context,e.time,self,out,nullptr,error,cap);if(status)return status;for(uint64_t c=0;c<q.coordinates;++c){long double value=out[c];if(q.diffusion[c]!=0)for(auto at=e.network.pointers[node];at<e.network.pointers[node+1];++at){const auto arc=e.network.arcs[at];value+=e.network.coupling(arc,c,e.state.data()+arc.source*q.coordinates,self);}if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<double>::max())throw Error(PENTA_NUMERIC_DOMAIN,"Network RHS became nonfinite.");out[c]=static_cast<double>(value);}}return PENTA_NUMERIC_OK;}catch(const Error& ex){message(error,cap,ex.what());return ex.status;}catch(const std::exception& ex){message(error,cap,ex.what());return PENTA_NUMERIC_INTERNAL;}catch(...){return PENTA_NUMERIC_INTERNAL;}
        }
    };
    void evaluate(double t,const std::vector<double>& state,std::vector<double>& output){check(r.coordinates*(r.nodes+arcs.size()*(operators?r.coordinates:1)));Evaluation evaluation{*this,t,state,output};char error[2048]{};const auto cost=std::max<uint64_t>(32,r.coordinates*(1+arcs.size()/r.nodes));const int status=host.parallel_for?host.parallel_for(host.context,r.nodes,cost,Evaluation::run,&evaluation,error,sizeof(error)):Evaluation::run(&evaluation,0,r.nodes,error,sizeof(error));if(status)throw Error(status,error[0]?error:"Native network local RHS failed.");result.rhs_evaluations+=r.nodes;}
    void sample(uint64_t index,std::span<const double> state){
        check(r.nodes*r.coordinates);std::copy(state.begin(),state.end(),result.values+index*state.size());long double total=0;
        for(uint64_t c=0;c<r.coordinates;++c){long double mean=0;for(uint64_t node=0;node<r.nodes;++node)mean+=static_cast<long double>(state[node*r.coordinates+c])/r.nodes;if(!std::isfinite(mean)||std::abs(mean)>std::numeric_limits<double>::max())throw Error(PENTA_NUMERIC_DOMAIN,"Network mean overflowed.");result.means[index*r.coordinates+c]=static_cast<double>(mean);for(uint64_t node=0;node<r.nodes;++node){const long double delta=state[node*r.coordinates+c]-mean;total+=delta*delta/r.nodes;}}
        const auto rms=std::sqrt(total);if(!std::isfinite(rms)||rms>std::numeric_limits<double>::max())throw Error(PENTA_NUMERIC_DOMAIN,"Network synchronization norm overflowed.");result.synchronization[index]=static_cast<double>(rms);
    }
    #include "network_bdf.inc"
    void solve(){
        build();const auto count=r.nodes*r.coordinates;std::vector<double> state(r.initial,r.initial+count),candidate(count),k1(count),k2(count),k3(count),k4(count),next(count);sample(0,state);double time=r.times[0];
        for(uint64_t sampleIndex=1;sampleIndex<r.samples;++sampleIndex){const auto target=r.times[sampleIndex];const long double required=std::ceil((static_cast<long double>(target)-time)/r.step);if(required>r.max_steps-result.steps)throw Error(7,"Network RK4 step budget exceeded.");const auto steps=static_cast<uint64_t>(std::max(1.L,required));const double h=(target-time)/steps;if(!(h>0)||time+h==time)throw Error(PENTA_NUMERIC_ARGUMENT,"Network time step cannot advance binary64 time.");result.largest_step=std::max(result.largest_step,h);
            for(uint64_t step=0;step<steps;++step){check();const double at=step+1==steps?target:time+h;const double actual=at-time;evaluate(time,state,k1);for(uint64_t i=0;i<count;++i)candidate[i]=state[i]+.5*actual*k1[i];evaluate(time+.5*actual,candidate,k2);for(uint64_t i=0;i<count;++i)candidate[i]=state[i]+.5*actual*k2[i];evaluate(time+.5*actual,candidate,k3);for(uint64_t i=0;i<count;++i)candidate[i]=state[i]+actual*k3[i];evaluate(at,candidate,k4);for(uint64_t i=0;i<count;++i){const long double value=static_cast<long double>(state[i])+actual*(static_cast<long double>(k1[i])+2.L*k2[i]+2.L*k3[i]+k4[i])/6;if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<double>::max())throw Error(PENTA_NUMERIC_DOMAIN,"Network integration became nonfinite; reduce step or use a suitable stiff method.");next[i]=static_cast<double>(value);}state.swap(next);time=at;result.last_step=actual;++result.steps;}
            sample(sampleIndex,state);
        }
        result.work_units=work;result.elapsed_seconds=std::chrono::duration<double>(Clock::now()-start).count();
    }
};
int solveOperators(const penta_network_host_v1* host,const penta_network_request_v1* request,const double* operators,uint64_t operatorCount,penta_network_result_v1* result,char* error,uint64_t cap)noexcept{
    try{if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_NETWORK_ABI_V1||!host->local||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw Error(PENTA_NUMERIC_ARGUMENT,"Native network ABI is incomplete.");const auto& r=*request;if((r.method!=1&&r.method!=2)||!r.nodes||r.nodes>100000||r.edges>500000||!r.coordinates||r.coordinates>32||!r.samples||r.samples>100000||r.nodes>8000000/r.coordinates/r.samples||r.directed>1||r.general_coupling>1||!r.initial||!r.diffusion||!r.times||(r.edges&&(!r.endpoints||!r.weights))||!std::isfinite(r.step)||(r.method==1?r.step<=0:r.step<0)||!std::isfinite(r.max_seconds)||r.max_seconds<=0||!r.max_steps||!r.max_work)throw Error(PENTA_NUMERIC_ARGUMENT,"Network RK4 dimensions, method, time or work budgets are invalid.");if(operators){if(!r.general_coupling||r.edges>8000000/r.coordinates/r.coordinates||operatorCount!=r.edges*r.coordinates*r.coordinates)throw Error(PENTA_NUMERIC_ARGUMENT,"Edge operators require explicit general coupling and one bounded n-by-n operator per physical edge.");for(uint64_t at=0;at<operatorCount;++at)if(!std::isfinite(operators[at]))throw Error(PENTA_NUMERIC_ARGUMENT,"Edge operators must be finite.");}else if(operatorCount)throw Error(PENTA_NUMERIC_ARGUMENT,"Missing edge operator storage.");const auto count=r.nodes*r.coordinates;if(result->value_capacity<count*r.samples||result->mean_capacity<r.samples*r.coordinates||result->sample_capacity<r.samples||!result->values||!result->means||!result->synchronization)throw Error(PENTA_NUMERIC_ARGUMENT,"Network output buffers are insufficient.");for(uint64_t i=0;i<count;++i)if(!std::isfinite(r.initial[i]))throw Error(PENTA_NUMERIC_ARGUMENT,"Network initial coefficients must be finite.");for(uint64_t c=0;c<r.coordinates;++c)if(!std::isfinite(r.diffusion[c])||(!r.general_coupling&&r.diffusion[c]<0))throw Error(PENTA_NUMERIC_ARGUMENT,"Diffusion coefficients must be finite and nonnegative unless coupling=general.");for(uint64_t s=0;s<r.samples;++s)if(!std::isfinite(r.times[s])||(s&&r.times[s]<=r.times[s-1]))throw Error(PENTA_NUMERIC_ARGUMENT,"Network sample times must be finite and strictly increasing.");if(r.method==2&&(!std::isfinite(r.rtol)||r.rtol<=0||r.rtol>1||!std::isfinite(r.atol)||r.atol<=0||r.krylov_dimension<1||r.krylov_dimension>64))throw Error(PENTA_NUMERIC_ARGUMENT,"BDF tolerances or Krylov dimension are invalid.");const long double memory=sizeof(double)*(static_cast<long double>(r.samples)*(count+r.coordinates+2)+(r.method==2?static_cast<long double>(r.krylov_dimension+48)*count+2.L*count*r.coordinates:8.L*count))+32.L*r.edges;if(memory+operatorCount*sizeof(double)>1024.L*1024*1024)throw Error(PENTA_NUMERIC_ARGUMENT,"Network native workspace estimate exceeds1 GiB.");Network network{*host,r,*result,operators};if(r.method==2)network.solveBdf();else network.solve();return PENTA_NUMERIC_OK;}catch(const Error& ex){message(error,cap,ex.what());return ex.status;}catch(const std::exception& ex){message(error,cap,ex.what());return PENTA_NUMERIC_INTERNAL;}catch(...){message(error,cap,"Native network solve failed.");return PENTA_NUMERIC_INTERNAL;}
}
int solve(const penta_network_host_v1* h,const penta_network_request_v1* r,penta_network_result_v1* out,char* error,uint64_t cap)noexcept{return solveOperators(h,r,nullptr,0,out,error,cap);}
const penta_network_api_v1 api{sizeof(api),PENTA_NETWORK_ABI_V1,"penta_networks",solve};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_network_api_v1* penta_network_query_v1(uint32_t version){return version==PENTA_NETWORK_ABI_V1?&api:nullptr;}

extern "C" PENTA_NUMERIC_EXPORT const penta_network_operator_api_v1* penta_network_operator_query_v1(uint32_t abi){static const penta_network_operator_api_v1 operators{sizeof(operators),PENTA_NETWORK_OPERATOR_ABI_V1,"penta_network_operators",solveOperators};return abi==PENTA_NETWORK_OPERATOR_ABI_V1?&operators:nullptr;}
