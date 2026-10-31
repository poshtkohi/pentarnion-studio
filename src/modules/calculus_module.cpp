#include "calculus_api.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
struct Error final:std::runtime_error {int code;Error(int c,const char* text):std::runtime_error(text),code(c){}};
void require(bool okay,const char* text){if(!okay)throw Error(PENTA_NUMERIC_ARGUMENT,text);}
void checkCancel(const penta_numeric_host_v1* host){if(host&&host->cancelled&&host->cancelled(host->context))throw Error(PENTA_NUMERIC_CANCELLED,"Numerical calculus cancelled.");}
double checkedFinite(long double value){const double result=static_cast<double>(value);if(!std::isfinite(result))throw Error(PENTA_NUMERIC_DOMAIN,"Numerical calculus produced a nonfinite result.");return result;}
void message(char* error,uint64_t capacity,const char* text){if(error&&capacity)std::snprintf(error,static_cast<std::size_t>(capacity),"%s",text);}
void quadrature(const penta_numeric_host_v1* host,const penta_numeric_calculus_request_v1& r){
    require(r.integrand&&r.coordinates&&r.coordinate_count==2&&r.data&&r.data_capacity>=r.components,"Invalid quadrature callback/input buffers.");
    require(r.output&&r.output_capacity>=r.components,"Quadrature output buffer is too small.");
    require(r.maximum_intervals>=1&&r.maximum_intervals<=100000&&std::isfinite(r.absolute_tolerance)&&r.absolute_tolerance>0&&std::isfinite(r.relative_tolerance)&&r.relative_tolerance>=0,"Invalid quadrature tolerances or interval budget.");
    const double a=checkedFinite(r.coordinates[0]),b=checkedFinite(r.coordinates[1]);require(a<=b,"Quadrature callback coordinates must be increasing.");
    using Values=std::array<long double,5>;
    auto sample=[&](double coordinate){checkCancel(host);std::array<double,5> value{};char error[512]{};const int status=r.integrand(r.integrand_context,coordinate,value.data(),r.components,error,sizeof(error));
        if(status!=PENTA_NUMERIC_OK)throw Error(status,error[0]?error:"Quadrature integrand callback failed.");
        Values result{};for(uint32_t c=0;c<r.components;++c)result[c]=checkedFinite(value[c]);return result;};
    auto midpoint=[](double left,double right){return checkedFinite((static_cast<long double>(left)+right)/2);};
    auto simpson=[&](double left,double right,const Values& l,const Values& m,const Values& rr){Values result{};const long double h=(static_cast<long double>(right)-left)/6;for(uint32_t c=0;c<r.components;++c){result[c]=h*(l[c]+4*m[c]+rr[c]);checkedFinite(result[c]);}return result;};
    Values fa{};for(uint32_t c=0;c<r.components;++c)fa[c]=checkedFinite(r.data[c]);
    if(a==b){for(uint32_t c=0;c<r.components;++c)r.output[c]=0;return;}
    const double middle=midpoint(a,b);require(middle!=a&&middle!=b,"Quadrature endpoints are too close for representable subdivision.");
    const auto fm=sample(middle),fb=sample(b),initial=simpson(a,b,fa,fm,fb);long double scale=0;for(uint32_t c=0;c<r.components;++c)scale=std::max(scale,std::abs(initial[c]));
    const long double tolerance=std::max(static_cast<long double>(r.absolute_tolerance),static_cast<long double>(r.relative_tolerance)*scale);
    struct Interval {double a,b;long double tolerance;Values fa,fm,fb,estimate;uint32_t depth;};
    std::vector<Interval> stack{{a,b,tolerance,fa,fm,fb,initial,0}};Values total{};uint64_t leaves=1;
    while(!stack.empty()){
        checkCancel(host);auto current=std::move(stack.back());stack.pop_back();const auto m=midpoint(current.a,current.b),l=midpoint(current.a,m),rr=midpoint(m,current.b);
        require(l!=current.a&&l!=m&&rr!=m&&rr!=current.b,"Quadrature subdivision cannot advance at this precision.");
        const auto fl=sample(l),fr=sample(rr),left=simpson(current.a,m,current.fa,fl,current.fm),right=simpson(m,current.b,current.fm,fr,current.fb);Values refined{},difference{};long double error=0;
        for(uint32_t c=0;c<r.components;++c){refined[c]=left[c]+right[c];difference[c]=refined[c]-current.estimate[c];error=std::max(error,std::abs(difference[c])/15);}
        if(error<=current.tolerance){for(uint32_t c=0;c<r.components;++c)total[c]+=refined[c]+difference[c]/15;continue;}
        if(leaves>=r.maximum_intervals||current.depth>=50)throw Error(PENTA_NUMERIC_DOMAIN,"Adaptive Simpson quadrature did not converge within max_intervals/depth.");
        ++leaves;stack.push_back({m,current.b,current.tolerance/2,current.fm,fr,current.fb,right,current.depth+1});stack.push_back({current.a,m,current.tolerance/2,current.fa,fl,current.fm,left,current.depth+1});
    }
    for(uint32_t c=0;c<r.components;++c)r.output[c]=checkedFinite(total[c]);
}
struct Work {
    const penta_numeric_host_v1* host;
    const penta_numeric_calculus_request_v1& r;
    uint64_t extent=0,lines=0;
    bool increasing=true;
    uint64_t inputIndex(uint64_t line,uint64_t position) const {return r.axis==0?position*r.columns+line:line*r.columns+position;}
    double input(uint64_t line,uint64_t position,uint32_t component) const {
        return checkedFinite(r.data[inputIndex(line,position)*r.value_stride+component]);
    }
    void output(uint64_t line,uint64_t position,uint32_t component,double value,uint64_t length) const {
        const uint64_t index=r.axis==0?position*r.columns+line:line*length+position;
        r.output[index*r.components+component]=value;
    }
    void run(uint64_t first,uint64_t last) const {
        for(uint64_t line=first;line<last;++line){
            checkCancel(host);
            if(r.operation==PENTA_CALC_INTERP_LINEAR||r.operation==PENTA_CALC_INTERP_HERMITE){
                const double q=checkedFinite(r.query[line]);
                if(extent==0)throw Error(PENTA_NUMERIC_ARGUMENT,"Interpolation requires at least one source sample.");
                const double low=std::min(r.coordinates[0],r.coordinates[extent-1]),high=std::max(r.coordinates[0],r.coordinates[extent-1]);
                if(!r.extrapolate&&(q<low||q>high))throw Error(PENTA_NUMERIC_DOMAIN,"Interpolation query is outside the sampled coordinate interval.");
                uint64_t right=0;
                if(extent>1){uint64_t begin=0,end=extent;while(begin<end){const auto middle=begin+(end-begin)/2;const bool after=increasing?r.coordinates[middle]<=q:r.coordinates[middle]>=q;if(after)begin=middle+1;else end=middle;}right=std::clamp<uint64_t>(begin,1,extent-1);}
                for(uint32_t c=0;c<r.components;++c){
                    if(extent==1){r.output[line*r.components+c]=checkedFinite(r.data[c]);continue;}
                    const auto left=right-1;const long double h=static_cast<long double>(r.coordinates[right])-r.coordinates[left];
                    const long double z=(static_cast<long double>(q)-r.coordinates[left])/h;
                    const long double a=checkedFinite(r.data[left*r.value_stride+c]),b=checkedFinite(r.data[right*r.value_stride+c]);
                    long double result=(1-z)*a+z*b;
                    if(r.operation==PENTA_CALC_INTERP_HERMITE){const long double da=checkedFinite(r.derivative_data[left*r.value_stride+c]),db=checkedFinite(r.derivative_data[right*r.value_stride+c]);
                        const auto z2=z*z,z3=z2*z;result=(2*z3-3*z2+1)*a+(z3-2*z2+z)*h*da+(-2*z3+3*z2)*b+(z3-z2)*h*db;}
                    r.output[line*r.components+c]=checkedFinite(result);
                }
                continue;
            }
            if(r.operation==PENTA_CALC_TRAPZ||r.operation==PENTA_CALC_CUMTRAPZ){
                std::vector<long double> total(r.components,0);
                for(uint64_t position=0;position<extent;++position){
                    if((position&255u)==0)checkCancel(host);
                    for(uint32_t c=0;c<r.components;++c){
                        if(position){const long double h=static_cast<long double>(r.coordinates[position])-r.coordinates[position-1];total[c]+=h*(static_cast<long double>(input(line,position-1,c))+input(line,position,c))/2;}
                        else static_cast<void>(input(line,0,c));
                        if(r.operation==PENTA_CALC_CUMTRAPZ)output(line,position,c,checkedFinite(total[c]),extent);
                    }
                }
                if(r.operation==PENTA_CALC_TRAPZ)for(uint32_t c=0;c<r.components;++c)r.output[line*r.components+c]=checkedFinite(total[c]);
                continue;
            }
            if(r.operation==PENTA_CALC_DIFF){
                const auto rounds=std::min<uint64_t>(r.difference_order,extent);const auto length=extent-rounds;
                std::vector<double> values(extent*r.components);
                for(uint64_t p=0;p<extent;++p)for(uint32_t c=0;c<r.components;++c)values[p*r.components+c]=input(line,p,c);
                for(uint64_t order=0;order<rounds;++order){checkCancel(host);for(uint64_t p=0;p<extent-order-1;++p)for(uint32_t c=0;c<r.components;++c)
                    values[p*r.components+c]=checkedFinite(static_cast<long double>(values[(p+1)*r.components+c])-values[p*r.components+c]);}
                for(uint64_t p=0;p<length;++p)for(uint32_t c=0;c<r.components;++c)output(line,p,c,values[p*r.components+c],length);
                continue;
            }
            for(uint64_t position=0;position<extent;++position){
                if((position&255u)==0)checkCancel(host);
                for(uint32_t c=0;c<r.components;++c){
                    long double result=0;
                    if(extent==2&&r.operation==PENTA_CALC_DERIVATIVE1)result=(static_cast<long double>(input(line,1,c))-input(line,0,c))/(static_cast<long double>(r.coordinates[1])-r.coordinates[0]);
                    else if(extent>=3){
                        const uint64_t start=position==0?0:position+1==extent?extent-3:position-1;
                        const long double x=r.coordinates[position];
                        for(uint64_t local=0;local<3;++local){const auto a=start+local,b=start+(local+1)%3,d=start+(local+2)%3;
                            const long double denominator=(static_cast<long double>(r.coordinates[a])-r.coordinates[b])*(static_cast<long double>(r.coordinates[a])-r.coordinates[d]);
                            const long double weight=r.operation==PENTA_CALC_DERIVATIVE2?2/denominator:(2*x-r.coordinates[b]-r.coordinates[d])/denominator;
                            result+=weight*input(line,a,c);
                        }
                    }else if(extent==1)static_cast<void>(input(line,0,c));
                    output(line,position,c,checkedFinite(result),extent);
                }
            }
        }
    }
};
int range(void* context,uint64_t first,uint64_t last,char* error,uint64_t capacity){try{static_cast<Work*>(context)->run(first,last);return PENTA_NUMERIC_OK;}catch(const Error& e){message(error,capacity,e.what());return e.code;}catch(const std::exception& e){message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){message(error,capacity,"Unexpected numerical calculus failure.");return PENTA_NUMERIC_INTERNAL;}}
}
extern "C" int penta_numeric_calculus_v1(const penta_numeric_host_v1* host,const penta_numeric_calculus_request_v1* request,char* error,uint64_t capacity){
    try{
        require(request&&request->struct_size>=sizeof(*request),"Invalid numerical calculus request ABI.");
        if(host)require(host->struct_size>=sizeof(*host)&&host->abi_version==PENTA_NUMERIC_ABI_V1,"Invalid numerical calculus host ABI.");
        const auto& r=*request;require(r.components==1||r.components==5,"Calculus values require one real or five Pentarnion coefficients.");
        require(r.axis<=1,"Calculus axis must be row or column.");
        require(r.operation>=PENTA_CALC_INTERP_LINEAR&&r.operation<=PENTA_CALC_QUADRATURE,"Unknown numerical calculus operation.");
        if(r.operation==PENTA_CALC_QUADRATURE){checkCancel(host);quadrature(host,r);return PENTA_NUMERIC_OK;}
        require(r.rows<=8000000&&r.columns<=100000&&(!r.columns||r.rows<=8000000/r.columns),"Calculus dimensions exceed the native coordinate bound.");
        require(r.value_stride>=r.components,"Invalid calculus input coefficient stride.");
        const uint64_t inputCount=r.rows*r.columns;require(inputCount<=8000000/r.components,"Calculus input exceeds its real-coordinate bound.");require(!inputCount||(r.data&&inputCount-1<=(std::numeric_limits<uint64_t>::max()-r.components)/r.value_stride&&r.data_capacity>=(inputCount-1)*r.value_stride+r.components),"Calculus input buffer is too small.");
        Work work{host,r};work.extent=r.axis==0?r.rows:r.columns;work.lines=r.axis==0?r.columns:r.rows;
        if(r.operation==PENTA_CALC_DERIVATIVE2&&inputCount)
            require(work.extent>=3,"Insufficient data: a second derivative requires at least three samples along the differentiated dimension.");
        const bool interpolation=r.operation==PENTA_CALC_INTERP_LINEAR||r.operation==PENTA_CALC_INTERP_HERMITE;
        if(interpolation){require(r.columns==1,"Interpolation expects a sample column.");work.extent=r.rows;work.lines=r.query_count;require(!r.query_count||r.query,"Missing interpolation queries.");}
        if(r.operation!=PENTA_CALC_DIFF){require(r.coordinate_count==work.extent&&(!work.extent||r.coordinates),"Calculus coordinates must match the selected extent.");
            for(uint64_t p=0;p<work.extent;++p){if((p&255u)==0)checkCancel(host);checkedFinite(r.coordinates[p]);if(p){const long double delta=static_cast<long double>(r.coordinates[p])-r.coordinates[p-1];require(delta!=0,"Calculus coordinates must be strictly monotone.");if(p==1)work.increasing=delta>0;else require((delta>0)==work.increasing,"Calculus coordinates must be strictly monotone.");}}}
        if(r.operation==PENTA_CALC_INTERP_HERMITE)require(r.derivative_data&&r.derivative_capacity>=r.data_capacity,"Dense interpolation derivative data is missing or incomplete.");
        uint64_t outputCount=inputCount;
        if(interpolation)outputCount=r.query_count;
        else if(r.operation==PENTA_CALC_TRAPZ)outputCount=work.lines;
        else if(r.operation==PENTA_CALC_DIFF)outputCount=work.lines*(work.extent-std::min<uint64_t>(r.difference_order,work.extent));
        require(outputCount<=8000000/r.components&&r.output_capacity>=outputCount*r.components&&(!outputCount||r.output),"Calculus output buffer is too small.");
        checkCancel(host);
        if(!work.lines)return PENTA_NUMERIC_OK;
        if(host&&host->parallel_for)return host->parallel_for(host->context,work.lines,std::max<uint64_t>(1,work.extent*r.components),range,&work,error,capacity);
        return range(&work,0,work.lines,error,capacity);
    }catch(const Error& e){message(error,capacity,e.what());return e.code;}catch(const std::exception& e){message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){message(error,capacity,"Unexpected numerical calculus failure.");return PENTA_NUMERIC_INTERNAL;}
}
