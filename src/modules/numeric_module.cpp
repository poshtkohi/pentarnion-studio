#define PENTA_NUMERIC_MODULE_BUILD
#include "numeric_api.h"
#include "calculus_api.h"
#include "algebra/pentarnion.hpp"
#include "algebra/elementary.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using P=penta::algebra::Pentarnion;
struct Error:std::runtime_error {int status;Error(int s,const std::string& message):std::runtime_error(message),status(s){}};
void message(char* output,uint64_t capacity,const std::string& text){if(output&&capacity){const auto n=std::min<uint64_t>(capacity-1,text.size());std::memcpy(output,text.data(),static_cast<std::size_t>(n));output[n]=0;}}
void require(bool okay,const char* error){if(!okay)throw Error(PENTA_NUMERIC_ARGUMENT,error);}
double finite(double value){if(!std::isfinite(value))throw Error(PENTA_NUMERIC_DOMAIN,"The result is outside the finite real domain.");return value;}
template<class F>int protect(F body,char* error,uint64_t capacity) noexcept {
    try{body();return PENTA_NUMERIC_OK;}
    catch(const Error& e){message(error,capacity,e.what());return e.status;}
    catch(const penta::algebra::AlgebraError& e){message(error,capacity,e.code+": "+e.what());return e.code=="R3005"||e.code=="R3006"||e.code=="R3007"||e.code=="R3008"?PENTA_NUMERIC_DIVISION:PENTA_NUMERIC_DOMAIN;}
    catch(const std::exception& e){message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}
    catch(...){message(error,capacity,"Unknown failure in the numerical module.");return PENTA_NUMERIC_INTERNAL;}
}
void hostValid(const penta_numeric_host_v1* host){require(host&&host->struct_size>=sizeof(penta_numeric_host_v1)&&host->abi_version==PENTA_NUMERIC_ABI_V1&&host->cancelled&&host->parallel_for,"Incompatible numerical host API.");if(host->cancelled(host->context))throw Error(PENTA_NUMERIC_CANCELLED,"Numerical execution cancelled.");}
P entry(const double* data,uint64_t index,uint32_t width,const penta::algebra::Context& context){
    penta::algebra::Coefficients values{};for(uint32_t c=0;c<width;++c)values[c]=finite(data[index*width+c]);return P(context,values);
}
bool unary(uint32_t op){return op>=PENTA_VEC_SIN||op==PENTA_VEC_NEGATE;}
double realOperation(uint32_t op,double a,double b){
    switch(op){
    case PENTA_VEC_ADD:return finite(a+b);case PENTA_VEC_SUBTRACT:return finite(a-b);
    case PENTA_VEC_MULTIPLY:return finite(a*b);case PENTA_VEC_DIVIDE:if(b==0)throw Error(PENTA_NUMERIC_DIVISION,"Division by zero.");return finite(a/b);
    case PENTA_VEC_POWER:return finite(std::pow(a,b));case PENTA_VEC_NEGATE:return -a;
    case PENTA_VEC_SIN:return finite(std::sin(a));case PENTA_VEC_COS:return finite(std::cos(a));case PENTA_VEC_EXP:return finite(std::exp(a));
    case PENTA_VEC_SINH:return finite(std::sinh(a));case PENTA_VEC_COSH:return finite(std::cosh(a));
    case PENTA_VEC_SQRT:return finite(std::sqrt(a));case PENTA_VEC_TAN:return finite(std::tan(a));
    case PENTA_VEC_ASIN:return finite(std::asin(a));case PENTA_VEC_ACOS:return finite(std::acos(a));case PENTA_VEC_ATAN:return finite(std::atan(a));
    case PENTA_VEC_TANH:return finite(std::tanh(a));case PENTA_VEC_ASINH:return finite(std::asinh(a));case PENTA_VEC_ACOSH:return finite(std::acosh(a));case PENTA_VEC_ATANH:return finite(std::atanh(a));
    case PENTA_VEC_LOG:return finite(std::log(a));case PENTA_VEC_LOG10:return finite(std::log10(a));case PENTA_VEC_LOG2:return finite(std::log2(a));
    case PENTA_VEC_ABS:return std::abs(a);case PENTA_VEC_FLOOR:return std::floor(a);case PENTA_VEC_CEIL:return std::ceil(a);case PENTA_VEC_ROUND:return std::round(a);
    case PENTA_VEC_CONJUGATE:case PENTA_VEC_SCALAR_PART:return a;
    case PENTA_VEC_NORM2:return finite(a*a);
    case PENTA_VEC_EXPM1:return finite(std::expm1(a));case PENTA_VEC_LOG1P:return finite(std::log1p(a));
    case PENTA_VEC_ERF:return finite(std::erf(a));case PENTA_VEC_ERFC:return finite(std::erfc(a));
    case PENTA_VEC_SIGN:return a>0?1:a<0?-1:0;
    default:throw Error(PENTA_NUMERIC_ARGUMENT,"Unsupported real vector operation.");
    }
}
struct VectorContext {const penta_numeric_host_v1& host;const penta_numeric_vector_request_v1& request;};
int vectorRange(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity){
    auto& call=*static_cast<VectorContext*>(raw);const auto& r=call.request;
    return protect([&]{const penta::algebra::Context algebraContext(r.gamma);
        for(uint64_t n=begin;n<end;++n){
            if((n&63u)==0&&call.host.cancelled(call.host.context))throw Error(PENTA_NUMERIC_CANCELLED,"Numerical execution cancelled.");
            const P a=entry(r.a,r.a_count==1?0:n,r.a_width,algebraContext);
            const P b=unary(r.operation)?P(algebraContext):entry(r.b,r.b_count==1?0:n,r.b_width,algebraContext);
            const auto ac=a.coefficients()[0],bc=b.coefficients()[0];P result(algebraContext);
            const bool pent=r.a_width==5||(!unary(r.operation)&&r.b_width==5)||(r.operation==PENTA_VEC_AXPY&&r.alpha_width==5);
            if(r.operation==PENTA_VEC_AXPY){const auto alpha=entry(r.alpha,0,r.alpha_width,algebraContext);result=penta::algebra::add(penta::algebra::multiply(alpha,a),b);}
            else if(r.operation==PENTA_VEC_PURE_PART)result=penta::algebra::purePart(a);
            else if(!pent)result=P::scalar(algebraContext,realOperation(r.operation,ac,bc));
            else switch(r.operation){
            case PENTA_VEC_ADD:result=penta::algebra::add(a,b);break;
            case PENTA_VEC_SUBTRACT:result=penta::algebra::subtract(a,b);break;
            case PENTA_VEC_MULTIPLY:result=penta::algebra::multiply(a,b);break;
            case PENTA_VEC_DIVIDE:if(r.b_width!=1)throw Error(PENTA_NUMERIC_ARGUMENT,"Vector division requires real divisors.");if(bc==0)throw Error(PENTA_NUMERIC_DIVISION,"Division by zero.");result=penta::algebra::scale(a,1/bc);break;
            case PENTA_VEC_POWER:if(r.b_width!=1||bc!=std::trunc(bc)||bc < -100000||bc > 100000)throw Error(PENTA_NUMERIC_ARGUMENT,"Pentarnion powers require integer exponents in -100000..100000.");result=penta::algebra::power(a,static_cast<std::int64_t>(bc));break;
            case PENTA_VEC_NEGATE:result=penta::algebra::scale(a,-1);break;
            case PENTA_VEC_SIN:result=penta::algebra::sine(a);break;case PENTA_VEC_COS:result=penta::algebra::cosine(a);break;
            case PENTA_VEC_EXP:result=penta::algebra::exponential(a);break;case PENTA_VEC_SINH:result=penta::algebra::hyperbolicSine(a);break;case PENTA_VEC_COSH:result=penta::algebra::hyperbolicCosine(a);break;
            case PENTA_VEC_CONJUGATE:result=penta::algebra::conjugate(a);break;
            case PENTA_VEC_SCALAR_PART:result=P::scalar(algebraContext,penta::algebra::scalarPart(a));break;
            case PENTA_VEC_NORM2:result=P::scalar(algebraContext,penta::algebra::normSquared(a));break;
            default:throw Error(PENTA_NUMERIC_ARGUMENT,"This elementwise function currently requires real vector entries.");
            }
            for(uint32_t c=0;c<r.output_width;++c)r.output[n*r.output_width+c]=finite(result.coefficients()[c]);
        }
    },error,capacity);
}
int vectorCall(const penta_numeric_host_v1* host,const penta_numeric_vector_request_v1* request,char* error,uint64_t capacity){
    int result=0;
    const auto validation=protect([&]{hostValid(host);require(request&&request->struct_size>=sizeof(*request),"Incompatible numerical vector request.");const auto& r=*request;
        require(r.count<=1000000,"Numerical vector exceeds the module element limit.");
        require((r.a_width==1||r.a_width==5)&&(r.output_width==1||r.output_width==5),"Invalid numerical coefficient width.");
        require(!r.count||(r.a&&(r.a_count==1||r.a_count==r.count)),"Invalid numerical input dimensions.");
        require(!r.count||(r.output&&r.output_capacity>=r.count*r.output_width),"Insufficient numerical output storage.");
        if(!unary(r.operation)){require((r.b_width==1||r.b_width==5)&&(!r.count||(r.b&&(r.b_count==1||r.b_count==r.count))),"Invalid second numerical vector.");}
        if(r.operation==PENTA_VEC_AXPY)require(r.alpha&&(r.alpha_width==1||r.alpha_width==5),"AXPY requires a real or Pentarnion left scalar.");
        const bool scalarResult=r.operation==PENTA_VEC_SCALAR_PART||r.operation==PENTA_VEC_NORM2;
        const uint32_t expectedWidth=scalarResult?1:r.operation==PENTA_VEC_PURE_PART?5:std::max({r.a_width,unary(r.operation)?1u:r.b_width,r.operation==PENTA_VEC_AXPY?r.alpha_width:1u});
        require(r.output_width==expectedWidth,"Numerical output coefficient width does not match the operation.");
        VectorContext context{*host,r};const auto cost=r.operation==PENTA_VEC_MULTIPLY&&expectedWidth==5?16u:r.operation>=PENTA_VEC_SIN?4u:1u;
        result=host->parallel_for(host->context,r.count,cost,vectorRange,&context,error,capacity);
    },error,capacity);
    return validation?validation:result;
}
constexpr std::size_t reductionBlock=256;
struct Partial {std::array<long double,5> sum{};long double scale=0,ssq=1,maximum=0;};
struct ReduceContext {const penta_numeric_host_v1& host;const penta_numeric_reduce_request_v1& request;std::vector<Partial>& partials;};
void addSquare(Partial& p,long double value){value=std::abs(value);if(value==0)return;if(p.scale<value){const auto ratio=p.scale/value;p.ssq=1+p.ssq*ratio*ratio;p.scale=value;}else{const auto ratio=value/p.scale;p.ssq+=ratio*ratio;}}
int reduceRange(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity){auto& call=*static_cast<ReduceContext*>(raw);const auto& r=call.request;
    return protect([&]{for(uint64_t block=begin;block<end;++block){auto& p=call.partials[block];const auto first=block*reductionBlock,last=std::min(r.count,first+reductionBlock);
        for(auto n=first;n<last;++n){if((n&63u)==0&&call.host.cancelled(call.host.context))throw Error(PENTA_NUMERIC_CANCELLED,"Numerical execution cancelled.");
            for(uint32_t c=0;c<r.width;++c){const auto a=finite(r.a[n*r.width+c]);
                switch(r.operation){
                case PENTA_REDUCE_SUM:case PENTA_REDUCE_MEAN:p.sum[c]+=a;break;
                case PENTA_REDUCE_DOT:p.sum[0]+=static_cast<long double>(a)*finite(r.b[n]);break;
                case PENTA_REDUCE_NORM:case PENTA_REDUCE_NORM_SQUARED:addSquare(p,a);break;
                case PENTA_REDUCE_NORM_L1:p.sum[0]+=std::abs(static_cast<long double>(a));break;
                case PENTA_REDUCE_NORM_INFINITY:p.maximum=std::max(p.maximum,std::abs(static_cast<long double>(a)));break;
                default:throw Error(PENTA_NUMERIC_ARGUMENT,"Unsupported reduction operation.");
                }
            }
        }
    }},error,capacity);
}
int reduceCall(const penta_numeric_host_v1* host,const penta_numeric_reduce_request_v1* request,char* error,uint64_t capacity){
    int result=0;
    const auto validation=protect([&]{hostValid(host);require(request&&request->struct_size>=sizeof(*request),"Incompatible numerical reduction request.");const auto& r=*request;
        require(r.count<=1000000&&(r.width==1||r.width==5),"Invalid numerical reduction dimensions.");require(r.count==0||r.a,"Numerical reduction input is missing.");
        if(r.operation==PENTA_REDUCE_DOT)require(r.width==1&&(r.count==0||r.b),"dot requires two real vectors.");
        if(r.operation>=PENTA_REDUCE_NORM)require(r.width==1,"Vector norm uses real vectors; Pentarnion scalar norm2 retains its separate definition.");
        const uint32_t width=r.operation==PENTA_REDUCE_SUM||r.operation==PENTA_REDUCE_MEAN?r.width:1;
        require(r.output&&r.output_capacity>=width,"Numerical reduction output storage is too small.");
        if(r.operation==PENTA_REDUCE_MEAN&&r.count==0)throw Error(PENTA_NUMERIC_DOMAIN,"The mean of an empty vector is outside the finite domain.");
        std::vector<Partial> partials((r.count+reductionBlock-1)/reductionBlock);ReduceContext context{*host,r,partials};
        // Fixed 256-entry blocks and fixed merge order give the same reduction
        // tree independently of how many CPU workers execute those blocks.
        result=host->parallel_for(host->context,partials.size(),reductionBlock,reduceRange,&context,error,capacity);if(result)return;
        Partial total;
        for(const auto& p:partials){for(uint32_t c=0;c<width;++c)total.sum[c]+=p.sum[c];total.maximum=std::max(total.maximum,p.maximum);
            if(p.scale!=0){if(total.scale<p.scale){const auto ratio=total.scale/p.scale;total.ssq=p.ssq+total.ssq*ratio*ratio;total.scale=p.scale;}else{const auto ratio=p.scale/total.scale;total.ssq+=p.ssq*ratio*ratio;}}}
        if(r.operation==PENTA_REDUCE_NORM)r.output[0]=finite(static_cast<double>(total.scale*std::sqrt(total.ssq)));
        else if(r.operation==PENTA_REDUCE_NORM_SQUARED)r.output[0]=finite(static_cast<double>(total.scale*total.scale*total.ssq));
        else if(r.operation==PENTA_REDUCE_NORM_INFINITY)r.output[0]=finite(static_cast<double>(total.maximum));
        else for(uint32_t c=0;c<width;++c)r.output[c]=finite(static_cast<double>(r.operation==PENTA_REDUCE_MEAN?total.sum[c]/r.count:total.sum[c]));
    },error,capacity);
    return validation?validation:result;
}
const penta_numeric_api_v1 api{sizeof(penta_numeric_api_v1),PENTA_NUMERIC_ABI_V1,"Pentarnion numerical module","numeric-abi1-fixed-reduction256-entrywise2",15,vectorCall,reduceCall,penta_numeric_calculus_v1};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_numeric_api_v1* penta_numeric_query_v1(uint32_t requested){return requested==PENTA_NUMERIC_ABI_V1?&api:nullptr;}
