#pragma once
#include <string>
#include <string_view>
namespace penta::native_functions::detail {
inline constexpr std::string_view thirdSupport=R"PENTA_JET3(
// Three commuting infinitesimals, each squared to zero. Coefficients of masks
// 1,2,4 are directional first derivatives; mask7 is D3f[u,v,w], without a
// factorial. This scalar extension never reassociates a Pentarnion product.
namespace penta_generated {
struct Jet3 {
    std::array<long double,8> c{};bool dependent=false;
    Jet3()=default;Jet3(long double value){c[0]=checked(value);}
    explicit operator bool()const{if(c[0]==0&&dependent)throw std::domain_error("Third derivative at a state-dependent branch boundary is unsupported.");return c[0]!=0;}
};
inline long double value(const Jet3& a){return a.c[0];}
inline Jet3 cb_unary(const Jet3& a,long double f,long double d,long double d2,long double d3){
    Jet3 r(f);r.dependent=a.dependent;if(!a.dependent)return r;
    for(const unsigned bit:{1u,2u,4u})r.c[bit]=checked(d*a.c[bit]);
    for(const unsigned mask:{3u,5u,6u}){const unsigned first=mask&(~mask+1u),second=mask^first;r.c[mask]=checked(d*a.c[mask]+d2*a.c[first]*a.c[second]);}
    r.c[7]=checked(d*a.c[7]+d2*(a.c[1]*a.c[6]+a.c[2]*a.c[5]+a.c[4]*a.c[3])+d3*a.c[1]*a.c[2]*a.c[4]);return r;
}
inline Jet3 operator+(const Jet3& a,const Jet3& b){Jet3 r;r.dependent=a.dependent||b.dependent;for(unsigned mask=0;mask<8;++mask)r.c[mask]=checked(a.c[mask]+b.c[mask]);return r;}
inline Jet3 operator-(const Jet3& a,const Jet3& b){Jet3 r;r.dependent=a.dependent||b.dependent;for(unsigned mask=0;mask<8;++mask)r.c[mask]=checked(a.c[mask]-b.c[mask]);return r;}
inline Jet3 operator-(const Jet3& a){Jet3 r;r.dependent=a.dependent;for(unsigned mask=0;mask<8;++mask)r.c[mask]=-a.c[mask];return r;}
inline Jet3 operator+(const Jet3& a){return a;}
inline Jet3 operator*(const Jet3& a,const Jet3& b){Jet3 r;r.dependent=a.dependent||b.dependent;for(unsigned mask=0;mask<8;++mask){long double sum=0;unsigned subset=mask;for(;;){sum=checked(sum+a.c[subset]*b.c[mask^subset]);if(subset==0)break;subset=(subset-1u)&mask;}r.c[mask]=sum;}return r;}
inline Jet3 operator/(const Jet3& a,const Jet3& b){if(b.c[0]==0)throw std::domain_error("Division by zero in third-order callback.");const auto inverse=1/b.c[0];return a*cb_unary(b,inverse,-inverse*inverse,2*inverse*inverse*inverse,-6*inverse*inverse*inverse*inverse);}
inline Jet3 divide(const Jet3& a,const Jet3& b){return a/b;}
inline bool cb_compare(const Jet3& a,const Jet3& b,unsigned operation){if(a.c[0]==b.c[0]&&(a.dependent||b.dependent))throw std::domain_error("Third derivative at a state-dependent comparison boundary is unsupported.");switch(operation){case 0:return a.c[0]==b.c[0];case 1:return a.c[0]!=b.c[0];case 2:return a.c[0]<b.c[0];case 3:return a.c[0]<=b.c[0];case 4:return a.c[0]>b.c[0];default:return a.c[0]>=b.c[0];}}
inline bool operator==(const Jet3& a,const Jet3& b){return cb_compare(a,b,0);}inline bool operator!=(const Jet3& a,const Jet3& b){return cb_compare(a,b,1);}
inline bool operator<(const Jet3& a,const Jet3& b){return cb_compare(a,b,2);}inline bool operator<=(const Jet3& a,const Jet3& b){return cb_compare(a,b,3);}
inline bool operator>(const Jet3& a,const Jet3& b){return cb_compare(a,b,4);}inline bool operator>=(const Jet3& a,const Jet3& b){return cb_compare(a,b,5);}
inline bool operator!(const Jet3& a){return !static_cast<bool>(a);}
inline Jet3 cb_sin(const Jet3& a){const auto s=std::sin(a.c[0]),c=std::cos(a.c[0]);return cb_unary(a,s,c,-s,-c);}
inline Jet3 cb_cos(const Jet3& a){const auto s=std::sin(a.c[0]),c=std::cos(a.c[0]);return cb_unary(a,c,-s,-c,s);}
inline Jet3 cb_exp(const Jet3& a){const auto e=checked(std::exp(a.c[0]));return cb_unary(a,e,e,e,e);}
inline Jet3 cb_expm1(const Jet3& a){const auto e=checked(std::exp(a.c[0]));return cb_unary(a,std::expm1(a.c[0]),e,e,e);}
inline Jet3 cb_log(const Jet3& a){if(a.c[0]<=0)throw std::domain_error("log requires positive input.");const auto inverse=1/a.c[0];return cb_unary(a,std::log(a.c[0]),inverse,-inverse*inverse,2*inverse*inverse*inverse);}
inline Jet3 cb_log1p(const Jet3& a){if(a.c[0]<=-1)throw std::domain_error("log1p requires input greater than-1.");const auto inverse=1/(1+a.c[0]);return cb_unary(a,std::log1p(a.c[0]),inverse,-inverse*inverse,2*inverse*inverse*inverse);}
inline Jet3 cb_log10(const Jet3& a){return cb_log(a)/Jet3(std::log(10.L));}inline Jet3 cb_log2(const Jet3& a){return cb_log(a)/Jet3(std::log(2.L));}
inline Jet3 cb_sqrt(const Jet3& a){if(a.c[0]<0)throw std::domain_error("sqrt requires nonnegative input.");if(a.c[0]==0){if(a.dependent)throw std::domain_error("Third derivative of state-dependent sqrt at zero cannot be certified.");return Jet3(0);}const auto root=std::sqrt(a.c[0]);return cb_unary(a,root,.5L/root,-.25L/(a.c[0]*root),.375L/(a.c[0]*a.c[0]*root));}
inline Jet3 cb_sinh(const Jet3& a){const auto s=std::sinh(a.c[0]),c=std::cosh(a.c[0]);return cb_unary(a,s,c,s,c);}
inline Jet3 cb_cosh(const Jet3& a){const auto s=std::sinh(a.c[0]),c=std::cosh(a.c[0]);return cb_unary(a,c,s,c,s);}
inline Jet3 cb_tanh(const Jet3& a){const auto t=std::tanh(a.c[0]),e=std::exp(-2*std::abs(a.c[0])),d=4*e/((1+e)*(1+e));return cb_unary(a,t,d,-2*t*d,4*t*t*d-2*d*d);}
inline Jet3 cb_tan(const Jet3& a){const auto t=std::tan(a.c[0]),d=1+t*t;return cb_unary(a,t,d,2*t*d,2*d*(1+3*t*t));}
inline Jet3 cb_asin(const Jet3& a){const auto x=a.c[0];if(std::abs(x)>=1){if(a.dependent||std::abs(x)>1)throw std::domain_error("asin third derivative requires input strictly inside[-1,1].");return Jet3(std::asin(x));}const auto q=1-x*x,r=std::sqrt(q);return cb_unary(a,std::asin(x),1/r,x/(q*r),(1+2*x*x)/(q*q*r));}
inline Jet3 cb_acos(const Jet3& a){const auto x=a.c[0];if(std::abs(x)>=1){if(a.dependent||std::abs(x)>1)throw std::domain_error("acos third derivative requires input strictly inside[-1,1].");return Jet3(std::acos(x));}const auto q=1-x*x,r=std::sqrt(q);return cb_unary(a,std::acos(x),-1/r,-x/(q*r),-(1+2*x*x)/(q*q*r));}
inline Jet3 cb_atan(const Jet3& a){const auto x=a.c[0],q=1+x*x;return cb_unary(a,std::atan(x),1/q,-2*x/(q*q),(6*x*x-2)/(q*q*q));}
inline Jet3 cb_asinh(const Jet3& a){const auto x=a.c[0],q=1+x*x,r=std::sqrt(q);return cb_unary(a,std::asinh(x),1/r,-x/(q*r),(2*x*x-1)/(q*q*r));}
inline Jet3 cb_acosh(const Jet3& a){const auto x=a.c[0];if(x<=1){if(a.dependent||x<1)throw std::domain_error("acosh third derivative requires input greater than1.");return Jet3(0);}const auto q=x*x-1,r=std::sqrt(q);return cb_unary(a,std::acosh(x),1/r,-x/(q*r),(2*x*x+1)/(q*q*r));}
inline Jet3 cb_atanh(const Jet3& a){const auto x=a.c[0];if(std::abs(x)>=1)throw std::domain_error("atanh requires input strictly inside(-1,1).");const auto q=1-x*x;return cb_unary(a,std::atanh(x),1/q,2*x/(q*q),2*(1+3*x*x)/(q*q*q));}
inline Jet3 cb_abs(const Jet3& a){if(a.c[0]==0&&a.dependent)throw std::domain_error("abs is nondifferentiable at zero.");return a.c[0]<0?-a:a;}
inline Jet3 cb_floor(const Jet3& a){if(a.dependent&&a.c[0]==std::floor(a.c[0]))throw std::domain_error("floor is discontinuous at an integer.");return Jet3(std::floor(a.c[0]));}
inline Jet3 cb_ceil(const Jet3& a){if(a.dependent&&a.c[0]==std::floor(a.c[0]))throw std::domain_error("ceil is discontinuous at an integer.");return Jet3(std::ceil(a.c[0]));}
inline Jet3 cb_round(const Jet3& a){if(a.dependent&&std::abs(a.c[0]-std::trunc(a.c[0]))==.5L)throw std::domain_error("round is discontinuous at a half-integer.");return Jet3(std::round(a.c[0]));}
inline Jet3 cb_sign(const Jet3& a){if(a.dependent&&a.c[0]==0)throw std::domain_error("sign is discontinuous at zero.");return Jet3(a.c[0]>0?1:a.c[0]<0?-1:0);}
inline Jet3 cb_erf(const Jet3& a){const auto x=a.c[0],d=2/std::sqrt(std::acos(-1.L))*std::exp(-x*x);return cb_unary(a,std::erf(x),d,-2*x*d,(4*x*x-2)*d);}
inline Jet3 cb_erfc(const Jet3& a){const auto x=a.c[0],d=-2/std::sqrt(std::acos(-1.L))*std::exp(-x*x);return cb_unary(a,std::erfc(x),d,-2*x*d,(4*x*x-2)*d);}
inline Jet3 cb_hypot(const Jet3& a,const Jet3& b){const auto scale=std::max(std::abs(a.c[0]),std::abs(b.c[0]));if(scale==0){if(a.dependent||b.dependent)throw std::domain_error("hypot is nondifferentiable at the origin.");return Jet3(0);}const auto x=a/Jet3(scale),y=b/Jet3(scale);return cb_sqrt(x*x+y*y)*Jet3(scale);}
inline Jet3 cb_atan2(const Jet3& y,const Jet3& x){if(x.c[0]==0&&y.c[0]==0)throw std::domain_error("atan2 is undefined at the origin.");if(x.c[0]<0&&y.c[0]==0&&(x.dependent||y.dependent))throw std::domain_error("atan2 third derivative crosses its branch cut.");Jet3 result=std::abs(x.c[0])>=std::abs(y.c[0])?cb_atan(y/x):-cb_atan(x/y);result.c[0]=checked(std::atan2(y.c[0],x.c[0]));return result;}
inline Jet3 cb_ipow(Jet3 a,long long exponent){if(exponent==0)return Jet3(1);const bool negative=exponent<0;unsigned long long n=negative?static_cast<unsigned long long>(-exponent):static_cast<unsigned long long>(exponent);Jet3 result(1);while(n){if(n&1u)result=result*a;n>>=1u;if(n)a=a*a;}return negative?Jet3(1)/result:result;}
inline Jet3 cb_even(const Jet3& w,bool hyper,bool sinc){
    if(w.c[0]<0)throw std::domain_error("Invalid pure-part squared length.");
    long double f=0,d=0,d2=0,d3=0;const long double sign=hyper?1:-1;
    if(w.c[0]<1){long double coefficient=1;for(unsigned k=0;k<32;++k){if(k){const unsigned a=sinc?2*k:2*k-1,b=sinc?2*k+1:2*k;coefficient*=sign/(static_cast<long double>(a)*b);}f+=coefficient*std::pow(w.c[0],static_cast<int>(k));if(k)d+=k*coefficient*std::pow(w.c[0],static_cast<int>(k-1));if(k>1)d2+=k*(k-1)*coefficient*std::pow(w.c[0],static_cast<int>(k-2));if(k>2)d3+=k*(k-1)*(k-2)*coefficient*std::pow(w.c[0],static_cast<int>(k-3));}}
    else{const auto root=std::sqrt(w.c[0]),c=hyper?std::cosh(root):std::cos(root),s=(hyper?std::sinh(root):std::sin(root))/root,q=w.c[0];if(sinc){f=s;d=(c-s)/(2*q);d2=(sign*q*s-3*c+3*s)/(4*q*q);d3=((sign*q+15)*c-(6*sign*q+15)*s)/(8*q*q*q);}else{f=c;d=sign*s/2;d2=sign*(c-s)/(4*q);d3=sign*(sign*q*s-3*c+3*s)/(8*q*q);}}
    return cb_unary(w,f,d,d2,d3);
}
inline Pent<Jet3> cb_whole(const Pent<Jet3>& p,unsigned operation){Jet3 w(0);for(unsigned n=1;n<5;++n)w=w+p.c[n]*p.c[n];Pent<Jet3> result;Jet3 amplitude;
    if(operation==1){result.c[0]=cb_sin(p.c[0])*cb_even(w,true,false);amplitude=cb_cos(p.c[0])*cb_even(w,true,true);}
    else if(operation==2){result.c[0]=cb_cos(p.c[0])*cb_even(w,true,false);amplitude=-cb_sin(p.c[0])*cb_even(w,true,true);}
    else if(operation==3){const auto e=cb_exp(p.c[0]);result.c[0]=e*cb_even(w,false,false);amplitude=e*cb_even(w,false,true);}
    else if(operation==4){result.c[0]=cb_sinh(p.c[0])*cb_even(w,false,false);amplitude=cb_cosh(p.c[0])*cb_even(w,false,true);}
    else{result.c[0]=cb_cosh(p.c[0])*cb_even(w,false,false);amplitude=cb_sinh(p.c[0])*cb_even(w,false,true);}
    for(unsigned n=1;n<5;++n)result.c[n]=amplitude*p.c[n];
    return result;
}
}
)PENTA_JET3";
inline std::string thirdEntry(std::size_t inputs,std::size_t outputs,std::size_t nodes){
    return R"PENTA3(
struct PentaThirdHostV1 {std::uint32_t struct_size,abi_version;void* context;int(*charge)(void*,std::uint64_t,char*,std::uint64_t);};
using PentaThirdEntryV1=int(*)(double,const double*,const double*,const double*,const double*,const unsigned char*,double*,double*,const PentaThirdHostV1*,char*,std::uint64_t);
struct PentaThirdApiV1 {std::uint32_t struct_size,abi_version,inputs,outputs;PentaThirdEntryV1 evaluate;};
struct PentaThirdBridge {const PentaThirdHostV1* host;static int charge(void* raw,std::size_t count,char* error,std::size_t capacity){const auto& bridge=*static_cast<PentaThirdBridge*>(raw);return bridge.host->charge(bridge.host->context,count,error,capacity);}};
CB_EXPORT int penta_callback_third_evaluate_v1(double gamma,const double* input,const double* su,const double* sv,const double* sw,const unsigned char* dependent,double* value,double* third,const PentaThirdHostV1* host,char* error,std::uint64_t capacity){try{
if(!host||host->struct_size<sizeof(PentaThirdHostV1)||host->abi_version!=65536u||!host->charge)throw std::domain_error("Invalid third-derivative host ABI.");
PentaThirdBridge bridge{host};CallbackHost bodyHost{1,&bridge,PentaThirdBridge::charge};
)PENTA3"+std::string("host_charge(&bodyHost,")+std::to_string(nodes*32+1)+");std::array<Jet3,"+std::to_string(inputs)+"> jets;for(unsigned n=0;n<"+std::to_string(inputs)+";++n){jets[n]=Jet3(input[n]);jets[n].c[1]=su?su[n]:0;jets[n].c[2]=sv?sv[n]:0;jets[n].c[4]=sw?sw[n]:0;jets[n].dependent=dependent&&dependent[n];}const auto result=callback_body(gamma,jets.data(),&bodyHost);std::array<double,"+std::to_string(outputs)+"> values{},thirds{};for(unsigned n=0;n<"+std::to_string(outputs)+";++n){values[n]=binary64(result[n].c[0]);thirds[n]=binary64(result[n].c[7]);}for(unsigned n=0;n<"+std::to_string(outputs)+";++n){value[n]=values[n];third[n]=thirds[n];}return 0;}catch(const std::exception& e){if(error&&capacity){std::strncpy(error,e.what(),capacity-1);error[capacity-1]=0;}return -1;}}\n"+
    "CB_EXPORT const PentaThirdApiV1* penta_callback_third_query_v1(std::uint32_t version){static const PentaThirdApiV1 api{sizeof(PentaThirdApiV1),65536u,"+std::to_string(inputs)+","+std::to_string(outputs)+",penta_callback_third_evaluate_v1};return version==65536u?&api:nullptr;}\nCB_EXPORT int penta_model_evaluate(double,double,const double*,const double*,double*,double*,char*,std::size_t){return -1;}\n";
}
}
