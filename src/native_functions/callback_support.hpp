#pragma once
#include <string_view>
namespace penta::native_functions::detail {
inline constexpr std::string_view callbackSupport=R"PENTA_CB(
// Second-order hyperdual arithmetic. Every direction is evaluated by the
// generated native expression tree; no interpreter callbacks enter this DLL.
namespace penta_generated {
struct Jet2 {
    long double v=0,x=0,y=0,xy=0;bool dependent=false;
    Jet2()=default;Jet2(long double a):v(checked(a)){}
    explicit operator bool()const{if(v==0&&dependent)throw std::domain_error("Derivative at a state-dependent branch boundary is unsupported.");return v!=0;}
};
inline long double value(const Jet2&a){return a.v;}
inline Jet2 cb_unary(const Jet2&a,long double f,long double d,long double d2){Jet2 r(f);r.dependent=a.dependent;r.x=checked(d*a.x);r.y=checked(d*a.y);r.xy=checked(d*a.xy+d2*a.x*a.y);return r;}
inline Jet2 operator+(const Jet2&a,const Jet2&b){Jet2 r(a.v+b.v);r.dependent=a.dependent||b.dependent;r.x=checked(a.x+b.x);r.y=checked(a.y+b.y);r.xy=checked(a.xy+b.xy);return r;}
inline Jet2 operator-(const Jet2&a,const Jet2&b){Jet2 r(a.v-b.v);r.dependent=a.dependent||b.dependent;r.x=checked(a.x-b.x);r.y=checked(a.y-b.y);r.xy=checked(a.xy-b.xy);return r;}
inline Jet2 operator-(const Jet2&a){Jet2 r(-a.v);r.dependent=a.dependent;r.x=-a.x;r.y=-a.y;r.xy=-a.xy;return r;}
inline Jet2 operator+(const Jet2&a){return a;}
inline Jet2 operator*(const Jet2&a,const Jet2&b){Jet2 r(a.v*b.v);r.dependent=a.dependent||b.dependent;r.x=checked(a.x*b.v+a.v*b.x);r.y=checked(a.y*b.v+a.v*b.y);r.xy=checked(a.xy*b.v+a.x*b.y+a.y*b.x+a.v*b.xy);return r;}
inline Jet2 operator/(const Jet2&a,const Jet2&b){if(b.v==0)throw std::domain_error("Division by zero in compiled callback.");const long double inverse=1/b.v;return a*cb_unary(b,inverse,-inverse*inverse,2*inverse*inverse*inverse);}
inline Jet2 divide(const Jet2&a,const Jet2&b){return a/b;}
inline bool cb_compare(const Jet2&a,const Jet2&b,unsigned op){if(a.v==b.v&&(a.dependent||b.dependent))throw std::domain_error("Derivative at a state-dependent comparison boundary is unsupported.");switch(op){case 0:return a.v==b.v;case 1:return a.v!=b.v;case 2:return a.v<b.v;case 3:return a.v<=b.v;case 4:return a.v>b.v;default:return a.v>=b.v;}}
inline bool operator==(const Jet2&a,const Jet2&b){return cb_compare(a,b,0);}inline bool operator!=(const Jet2&a,const Jet2&b){return cb_compare(a,b,1);}
inline bool operator<(const Jet2&a,const Jet2&b){return cb_compare(a,b,2);}inline bool operator<=(const Jet2&a,const Jet2&b){return cb_compare(a,b,3);}
inline bool operator>(const Jet2&a,const Jet2&b){return cb_compare(a,b,4);}inline bool operator>=(const Jet2&a,const Jet2&b){return cb_compare(a,b,5);}
inline bool operator!(const Jet2&a){return !static_cast<bool>(a);}
inline Jet2 cb_sin(const Jet2&a){return cb_unary(a,std::sin(a.v),std::cos(a.v),-std::sin(a.v));}
inline Jet2 cb_cos(const Jet2&a){return cb_unary(a,std::cos(a.v),-std::sin(a.v),-std::cos(a.v));}
inline Jet2 cb_exp(const Jet2&a){const auto e=checked(std::exp(a.v));return cb_unary(a,e,e,e);}
inline Jet2 cb_expm1(const Jet2&a){const auto e=checked(std::exp(a.v));return cb_unary(a,std::expm1(a.v),e,e);}
inline Jet2 cb_log(const Jet2&a){if(a.v<=0)throw std::domain_error("log requires positive input.");return cb_unary(a,std::log(a.v),1/a.v,-1/(a.v*a.v));}
inline Jet2 cb_log1p(const Jet2&a){if(a.v<=-1)throw std::domain_error("log1p requires input greater than -1.");const auto d=1/(1+a.v);return cb_unary(a,std::log1p(a.v),d,-d*d);}
inline Jet2 cb_log10(const Jet2&a){return cb_log(a)/Jet2(std::log(10.L));}inline Jet2 cb_log2(const Jet2&a){return cb_log(a)/Jet2(std::log(2.L));}
inline Jet2 cb_sqrt(const Jet2&a){if(a.v<0)throw std::domain_error("sqrt requires nonnegative input.");if(a.v==0){if(a.dependent)throw std::domain_error("Cannot certify a derivative of state-dependent sqrt at zero.");return Jet2(0);}const auto root=std::sqrt(a.v);return cb_unary(a,root,.5L/root,-.25L/(a.v*root));}
inline Jet2 cb_sinh(const Jet2&a){return cb_unary(a,std::sinh(a.v),std::cosh(a.v),std::sinh(a.v));}
inline Jet2 cb_cosh(const Jet2&a){return cb_unary(a,std::cosh(a.v),std::sinh(a.v),std::cosh(a.v));}
inline Jet2 cb_tanh(const Jet2&a){const auto t=std::tanh(a.v),e=std::exp(-2*std::abs(a.v)),d=4*e/((1+e)*(1+e));return cb_unary(a,t,d,-2*t*d);}
inline Jet2 cb_tan(const Jet2&a){const auto t=std::tan(a.v),d=1+t*t;return cb_unary(a,t,d,2*t*d);}
inline Jet2 cb_asin(const Jet2&a){if(std::abs(a.v)>=1){if(a.dependent||std::abs(a.v)>1)throw std::domain_error("asin derivative requires input strictly inside [-1,1].");return Jet2(std::asin(a.v));}const auto q=1-a.v*a.v,r=std::sqrt(q);return cb_unary(a,std::asin(a.v),1/r,a.v/(q*r));}
inline Jet2 cb_acos(const Jet2&a){if(std::abs(a.v)>=1){if(a.dependent||std::abs(a.v)>1)throw std::domain_error("acos derivative requires input strictly inside [-1,1].");return Jet2(std::acos(a.v));}const auto q=1-a.v*a.v,r=std::sqrt(q);return cb_unary(a,std::acos(a.v),-1/r,-a.v/(q*r));}
inline Jet2 cb_atan(const Jet2&a){const auto q=1+a.v*a.v;return cb_unary(a,std::atan(a.v),1/q,-2*a.v/(q*q));}
inline Jet2 cb_asinh(const Jet2&a){const auto q=1+a.v*a.v,r=std::sqrt(q);return cb_unary(a,std::asinh(a.v),1/r,-a.v/(q*r));}
inline Jet2 cb_acosh(const Jet2&a){if(a.v<=1){if(a.dependent||a.v<1)throw std::domain_error("acosh derivative requires input greater than1.");return Jet2(0);}const auto q=a.v*a.v-1,r=std::sqrt(q);return cb_unary(a,std::acosh(a.v),1/r,-a.v/(q*r));}
inline Jet2 cb_atanh(const Jet2&a){if(std::abs(a.v)>=1)throw std::domain_error("atanh requires input strictly inside (-1,1).");const auto q=1-a.v*a.v;return cb_unary(a,std::atanh(a.v),1/q,2*a.v/(q*q));}
inline Jet2 cb_abs(const Jet2&a){if(a.v==0&&a.dependent)throw std::domain_error("abs is nondifferentiable at zero.");return a.v<0?-a:a;}
inline Jet2 cb_floor(const Jet2&a){if(a.dependent&&a.v==std::floor(a.v))throw std::domain_error("floor is discontinuous at an integer.");return Jet2(std::floor(a.v));}
inline Jet2 cb_ceil(const Jet2&a){if(a.dependent&&a.v==std::floor(a.v))throw std::domain_error("ceil is discontinuous at an integer.");return Jet2(std::ceil(a.v));}
inline Jet2 cb_round(const Jet2&a){if(a.dependent&&std::abs(a.v-std::trunc(a.v))==.5L)throw std::domain_error("round is discontinuous at a half-integer.");return Jet2(std::round(a.v));}
inline Jet2 cb_sign(const Jet2&a){if(a.dependent&&a.v==0)throw std::domain_error("sign is discontinuous at zero.");return Jet2(a.v>0?1:a.v<0?-1:0);}
inline Jet2 cb_erf(const Jet2&a){const auto d=2/std::sqrt(std::acos(-1.L))*std::exp(-a.v*a.v);return cb_unary(a,std::erf(a.v),d,-2*a.v*d);}
inline Jet2 cb_erfc(const Jet2&a){const auto d=-2/std::sqrt(std::acos(-1.L))*std::exp(-a.v*a.v);return cb_unary(a,std::erfc(a.v),d,-2*a.v*d);}
inline Jet2 cb_hypot(const Jet2&a,const Jet2&b){const auto scale=std::max(std::abs(a.v),std::abs(b.v));if(scale==0){if(a.dependent||b.dependent)throw std::domain_error("hypot is nondifferentiable at the origin.");return Jet2(0);}const auto x=a/Jet2(scale),y=b/Jet2(scale);return cb_sqrt(x*x+y*y)*Jet2(scale);}
inline Jet2 cb_atan2(const Jet2&y,const Jet2&x){const auto q=x.v*x.v+y.v*y.v;if(q==0)throw std::domain_error("atan2 is undefined at the origin.");if(x.v<0&&y.v==0&&(x.dependent||y.dependent))throw std::domain_error("atan2 derivative crosses its branch cut.");Jet2 r(std::atan2(y.v,x.v));r.dependent=x.dependent||y.dependent;const auto fx=-y.v/q,fy=x.v/q,fxx=2*x.v*y.v/(q*q),fyy=-fxx,fxy=(y.v*y.v-x.v*x.v)/(q*q);r.x=checked(fx*x.x+fy*y.x);r.y=checked(fx*x.y+fy*y.y);r.xy=checked(fx*x.xy+fy*y.xy+fxx*x.x*x.y+fyy*y.x*y.y+fxy*(x.x*y.y+y.x*x.y));return r;}
inline Jet2 cb_ipow(Jet2 a,long long n){if(n==0)return Jet2(1);const bool negative=n<0;unsigned long long power=negative?static_cast<unsigned long long>(-n):static_cast<unsigned long long>(n);Jet2 r(1);while(power){if(power&1u)r=r*a;power>>=1u;if(power)a=a*a;}return negative?Jet2(1)/r:r;}
template<class S>Pent<S> cb_inverse(const Pent<S>&a){return divide(conjugate(a),norm2(a));}
template<class S>Pent<S> cb_pent_power(Pent<S>a,long long n,double gamma){if(n<0){a=cb_inverse(a);n=-n;}return power(a,static_cast<unsigned>(n),gamma);}
inline Jet2 cb_even(const Jet2&w,bool hyper,bool sinc){
    if(w.v<0)throw std::domain_error("Invalid pure-part length.");
    long double f=0,d=0,d2=0;
    if(w.v<.01L){long double coefficient=1;const long double sign=hyper?1:-1;for(unsigned k=0;k<24;++k){if(k){const unsigned a=sinc?2*k:2*k-1,b=sinc?2*k+1:2*k;coefficient*=sign/(static_cast<long double>(a)*b);}f+=coefficient*std::pow(w.v,static_cast<int>(k));if(k)d+=k*coefficient*std::pow(w.v,static_cast<int>(k-1));if(k>1)d2+=k*(k-1)*coefficient*std::pow(w.v,static_cast<int>(k-2));}}
    else{const auto r=std::sqrt(w.v),c=hyper?std::cosh(r):std::cos(r),s=(hyper?std::sinh(r):std::sin(r))/r;if(sinc){f=s;d=(c-s)/(2*w.v);d2=((hyper?w.v:-w.v)*s-3*c+3*s)/(4*w.v*w.v);}else{f=c;d=(hyper?1:-1)*s/2;d2=(hyper?c-s:s-c)/(4*w.v);}}
    return cb_unary(w,f,d,d2);
}
inline Pent<Jet2> cb_whole(const Pent<Jet2>&p,unsigned op){Jet2 w(0);for(unsigned n=1;n<5;++n)w=w+p.c[n]*p.c[n];Pent<Jet2> r;Jet2 amplitude;
    if(op==1){r.c[0]=cb_sin(p.c[0])*cb_even(w,true,false);amplitude=cb_cos(p.c[0])*cb_even(w,true,true);}
    else if(op==2){r.c[0]=cb_cos(p.c[0])*cb_even(w,true,false);amplitude=-cb_sin(p.c[0])*cb_even(w,true,true);}
    else if(op==3){const auto e=cb_exp(p.c[0]);r.c[0]=e*cb_even(w,false,false);amplitude=e*cb_even(w,false,true);}
    else if(op==4){r.c[0]=cb_sinh(p.c[0])*cb_even(w,false,false);amplitude=cb_cosh(p.c[0])*cb_even(w,false,true);}
    else{r.c[0]=cb_cosh(p.c[0])*cb_even(w,false,false);amplitude=cb_sinh(p.c[0])*cb_even(w,false,true);}
    for(unsigned n=1;n<5;++n)r.c[n]=amplitude*p.c[n];return r;
}
template<class S>bool pent_equal(const Pent<S>&a,const Pent<S>&b){bool equal=true;for(unsigned n=0;n<5;++n)equal=(a.c[n]==b.c[n])&&equal;return equal;}
}
)PENTA_CB";
}

