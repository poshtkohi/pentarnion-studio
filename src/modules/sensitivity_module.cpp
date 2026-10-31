#include "sensitivity_api.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>
namespace {
void message(char* out,uint64_t cap,const char* text){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
uint64_t mix(uint64_t n){n+=UINT64_C(0x9e3779b97f4a7c15);n=(n^(n>>30))*UINT64_C(0xbf58476d1ce4e5b9);n=(n^(n>>27))*UINT64_C(0x94d049bb133111eb);return n^(n>>31);}
double uniform(std::mt19937_64& random){return static_cast<double>(random()>>11)*0x1p-53;}
uint64_t index(std::mt19937_64& random,uint64_t count){const auto limit=uint64_t(-count)%count;uint64_t n;do{n=random();}while(n<limit);return n%count;}
template<class Function>void range(const penta_numeric_host_v1* host,uint64_t count,uint64_t cost,Function&& function){
    struct Task{Function& function;static int run(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t cap)noexcept{try{static_cast<Task*>(raw)->function(begin,end);return 0;}catch(const std::exception& e){message(error,cap,e.what());return 4;}catch(...){message(error,cap,"Unknown sensitivity worker failure.");return 4;}}}task{function};
    if(host->parallel_for){char detail[2048]{};if(host->parallel_for(host->context,count,cost,Task::run,&task,detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"Sensitivity workers failed.");}else function(0,count);
}
struct Estimates {double mean=0,variance=0;std::vector<double> first,total;};
Estimates estimate(const std::vector<double>& values,uint64_t n,unsigned d,const std::vector<uint64_t>* selection=nullptr){
    Estimates e;e.first.resize(d);e.total.resize(d);const uint64_t stride=d+2;
    auto row=[&](uint64_t r){return values.data()+(selection?(*selection)[r]:r)*stride;};
    // Shift by one observed value before summation to avoid losing small
    // variation on a large constant offset. Use population variance consistently.
    const double origin=row(0)[0];long double sum=0;
    for(uint64_t r=0;r<n;++r)sum+=(static_cast<long double>(row(r)[0])-origin)+(static_cast<long double>(row(r)[1])-origin);
    const long double center=static_cast<long double>(origin)+sum/(2*n);long double variance=0;
    for(uint64_t r=0;r<n;++r)for(unsigned a=0;a<2;++a){const auto delta=static_cast<long double>(row(r)[a])-center;variance+=delta*delta;}
    variance/=2*n;e.mean=static_cast<double>(center);e.variance=static_cast<double>(variance);
    if(!std::isfinite(e.mean)||!std::isfinite(e.variance)||e.variance<=0)throw std::runtime_error("Sobol indices require a finite positive sampled output variance.");
    for(unsigned j=0;j<d;++j){long double first=0,total=0;for(uint64_t r=0;r<n;++r){const auto* y=row(r);const long double delta=static_cast<long double>(y[2+j])-y[0];first+=(static_cast<long double>(y[1])-center)*delta;total+=delta*delta;}e.first[j]=static_cast<double>(first/(n*variance));e.total[j]=static_cast<double>(total/(2*n*variance));if(!std::isfinite(e.first[j])||!std::isfinite(e.total[j]))throw std::runtime_error("Sobol estimator overflowed.");}return e;
}
double quantile(std::vector<double>& values,double p){std::sort(values.begin(),values.end());const double x=p*(values.size()-1);const auto i=static_cast<std::size_t>(x);return values[i]+(x-i)*(values[std::min(i+1,values.size()-1)]-values[i]);}
int analyze(const penta_numeric_host_v1* host,const penta_sensitivity_request_v1* request,penta_sensitivity_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_NUMERIC_ABI_V1||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw std::runtime_error("Incompatible sensitivity ABI.");
        const auto& r=*request;auto& out=*result;const unsigned d=r.dimensions;
        if(!d||d>32||r.method>1||!r.samples||r.samples>200000||!r.lower||!r.upper||!r.evaluate||out.capacity<d||!out.mu||!out.mu_star||!out.sigma||!out.first||!out.total||!out.first_lower||!out.first_upper||!out.total_lower||!out.total_upper||r.bootstrap>2000||!std::isfinite(r.confidence)||r.confidence<=0||r.confidence>=1||!std::isfinite(r.max_wall_seconds)||r.max_wall_seconds<=0)throw std::runtime_error("Invalid sensitivity shape, sampling or resource settings.");
        if(r.method==0&&(r.levels<4||r.levels>100||r.levels%2||r.samples<2||r.bootstrap))throw std::runtime_error("Morris requires even levels in4..100, at least two trajectories, and bootstrap=0.");
        if(r.method==1&&r.samples<16)throw std::runtime_error("Sobol requires at least sixteen independent matched rows.");
        const uint64_t evaluations=r.samples*(d+(r.method?2:1));if(evaluations>r.max_evaluations||evaluations>8000000||r.bootstrap*r.samples*d>200000000)throw std::runtime_error("Sensitivity evaluation/workspace/bootstrap budget exceeded.");
        std::array<double,32> widths{};for(unsigned j=0;j<d;++j){widths[j]=r.upper[j]-r.lower[j];if(!std::isfinite(r.lower[j])||!std::isfinite(r.upper[j])||!std::isfinite(widths[j])||widths[j]<=0)throw std::runtime_error("Sensitivity bounds must be finite strictly increasing intervals.");}
        const auto started=std::chrono::steady_clock::now();out.evaluations=0;out.mean=out.variance=0;
        auto check=[&]{if(host->cancelled&&host->cancelled(host->context))throw std::runtime_error("Sensitivity cancelled.");if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>r.max_wall_seconds)throw std::runtime_error("Sensitivity wall-time budget exceeded.");};
        std::atomic_uint64_t attempted{0};
        auto evaluate=[&](const std::array<double,32>& point){check();char detail[2048]{};double value=0;attempted.fetch_add(1,std::memory_order_relaxed);if(r.evaluate(r.callback_context,point.data(),d,&value,detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"Native sensitivity model failed.");if(!std::isfinite(value))throw std::runtime_error("Sensitivity model returned a non-finite output.");return value;};
        if(r.method==0){
            std::vector<double> effects(r.samples*d);const unsigned step=r.levels/2;const double delta=static_cast<double>(step)/(r.levels-1);
            range(host,r.samples,(d+1)*128,[&](uint64_t begin,uint64_t end){for(uint64_t row=begin;row<end;++row){std::mt19937_64 random(mix(r.seed^mix(row)));std::array<double,32> point{},grid{};std::array<int,32> signs{};std::array<unsigned,32> permutation{};
                for(unsigned j=0;j<d;++j){signs[j]=index(random,2)?1:-1;const auto start=index(random,r.levels-step);grid[j]=static_cast<double>(start+(signs[j]<0?step:0))/(r.levels-1);point[j]=r.lower[j]+widths[j]*grid[j];permutation[j]=j;}
                for(unsigned j=d;j>1;--j)std::swap(permutation[j-1],permutation[index(random,j)]);
                double previous=evaluate(point);
                for(unsigned k=0;k<d;++k){const auto j=permutation[k];grid[j]+=signs[j]*delta;point[j]=r.lower[j]+widths[j]*grid[j];const double next=evaluate(point);effects[row*d+j]=(next-previous)/(signs[j]*delta);if(!std::isfinite(effects[row*d+j]))throw std::runtime_error("Morris elementary effect overflowed.");previous=next;}
            }});
            for(unsigned j=0;j<d;++j){long double sum=0,absolute=0;for(uint64_t row=0;row<r.samples;++row){sum+=effects[row*d+j];absolute+=std::abs(effects[row*d+j]);}out.mu[j]=static_cast<double>(sum/r.samples);out.mu_star[j]=static_cast<double>(absolute/r.samples);long double variance=0;for(uint64_t row=0;row<r.samples;++row){const long double delta=effects[row*d+j]-out.mu[j];variance+=delta*delta;}out.sigma[j]=std::sqrt(static_cast<double>(variance/(r.samples-1)));if(!std::isfinite(out.mu[j])||!std::isfinite(out.mu_star[j])||!std::isfinite(out.sigma[j]))throw std::runtime_error("Morris summary overflowed.");out.first[j]=out.total[j]=out.first_lower[j]=out.first_upper[j]=out.total_lower[j]=out.total_upper[j]=0;}
        }else{
            std::vector<double> values(evaluations);
            range(host,r.samples,(d+2)*128,[&](uint64_t begin,uint64_t end){for(uint64_t row=begin;row<end;++row){std::mt19937_64 random(mix(r.seed^mix(row)));std::array<double,32> a{},b{};for(unsigned j=0;j<d;++j){a[j]=r.lower[j]+widths[j]*uniform(random);b[j]=r.lower[j]+widths[j]*uniform(random);}auto* y=values.data()+row*(d+2);y[0]=evaluate(a);y[1]=evaluate(b);for(unsigned j=0;j<d;++j){const double saved=a[j];a[j]=b[j];y[2+j]=evaluate(a);a[j]=saved;}}});
            check();const auto e=estimate(values,r.samples,d);out.mean=e.mean;out.variance=e.variance;
            for(unsigned j=0;j<d;++j){out.mu[j]=out.mu_star[j]=out.sigma[j]=0;out.first[j]=e.first[j];out.total[j]=e.total[j];out.first_lower[j]=out.first_upper[j]=e.first[j];out.total_lower[j]=out.total_upper[j]=e.total[j];}
            if(r.bootstrap){
                std::vector<double> first(r.bootstrap*d),total(r.bootstrap*d);
                range(host,r.bootstrap,r.samples*d,[&](uint64_t begin,uint64_t end){for(uint64_t b=begin;b<end;++b){check();std::mt19937_64 random(mix(r.seed^UINT64_C(0x9c5f837c27a318d7)^mix(b)));std::vector<uint64_t> rows(r.samples);for(auto& row:rows)row=index(random,r.samples);const auto replicate=estimate(values,r.samples,d,&rows);for(unsigned j=0;j<d;++j){first[b*d+j]=replicate.first[j];total[b*d+j]=replicate.total[j];}}});
                const double tail=(1-r.confidence)/2;std::vector<double> samples(r.bootstrap);for(unsigned j=0;j<d;++j){for(uint64_t b=0;b<r.bootstrap;++b)samples[b]=first[b*d+j];out.first_lower[j]=quantile(samples,tail);out.first_upper[j]=quantile(samples,1-tail);for(uint64_t b=0;b<r.bootstrap;++b)samples[b]=total[b*d+j];out.total_lower[j]=quantile(samples,tail);out.total_upper[j]=quantile(samples,1-tail);}
            }
        }
        check();out.evaluations=attempted.load(std::memory_order_relaxed);out.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();return 0;
    }catch(const std::exception& failure){message(error,capacity,failure.what());return 1;}catch(...){message(error,capacity,"Unknown sensitivity failure.");return 4;}
}
const penta_sensitivity_api_v1 api{sizeof(api),PENTA_SENSITIVITY_ABI_V1,analyze};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_sensitivity_api_v1* penta_sensitivity_query_v1(uint32_t version){return version==PENTA_SENSITIVITY_ABI_V1?&api:nullptr;}
