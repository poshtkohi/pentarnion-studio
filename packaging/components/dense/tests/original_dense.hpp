#pragma once
#include "pentarnion/native_base.hpp"
namespace penta_reference {

inline long double checked(long double x) {
    if(!std::isfinite(x))throw std::domain_error("Nonfinite native ODE expression.");
    return x;
}
template<std::size_t N> struct Dual {
    long double v=0;std::array<long double,N>d{};
    bool stateDependent=false;
    Dual()=default;Dual(long double x):v(x){}
};
inline long double value(long double x){return x;}
template<std::size_t N> long double value(const Dual<N>&x){return x.v;}
template<std::size_t N> Dual<N> operator+(const Dual<N>&a,const Dual<N>&b){
    Dual<N> r(checked(a.v+b.v));r.stateDependent=a.stateDependent||b.stateDependent;for(std::size_t i=0;i<N;++i)r.d[i]=checked(a.d[i]+b.d[i]);return r;}
template<std::size_t N> Dual<N> operator-(const Dual<N>&a,const Dual<N>&b){
    Dual<N> r(checked(a.v-b.v));r.stateDependent=a.stateDependent||b.stateDependent;for(std::size_t i=0;i<N;++i)r.d[i]=checked(a.d[i]-b.d[i]);return r;}
template<std::size_t N> Dual<N> operator-(const Dual<N>&a){
    Dual<N> r(-a.v);r.stateDependent=a.stateDependent;for(std::size_t i=0;i<N;++i)r.d[i]=-a.d[i];return r;}
template<std::size_t N> Dual<N> operator*(const Dual<N>&a,const Dual<N>&b){
    Dual<N> r(checked(a.v*b.v));r.stateDependent=a.stateDependent||b.stateDependent;for(std::size_t i=0;i<N;++i)r.d[i]=checked(a.d[i]*b.v+a.v*b.d[i]);return r;}
template<std::size_t N> Dual<N> operator/(const Dual<N>&a,const Dual<N>&b){
    if(b.v==0)throw std::domain_error("Division by zero in native ODE.");
    Dual<N> r(checked(a.v/b.v));r.stateDependent=a.stateDependent||b.stateDependent;for(std::size_t i=0;i<N;++i)r.d[i]=checked((a.d[i]-r.v*b.d[i])/b.v);return r;}
inline long double divide(long double a,long double b){if(b==0)throw std::domain_error("Division by zero in native ODE.");return checked(a/b);}
template<std::size_t N> Dual<N> divide(const Dual<N>&a,const Dual<N>&b){return a/b;}
template<class S> S integerPower(S a,unsigned exponent){S r(1);while(exponent){if(exponent&1u)r=r*a;exponent>>=1u;if(exponent)a=a*a;}return r;}
template<class S> struct Pent {std::array<S,5>c{};};
template<class S> Pent<S> pent(S a,S b,S c,S d,S e){return {{{a,b,c,d,e}}};}
template<class S> Pent<S> basis(unsigned i){Pent<S>r;r.c.at(i)=S(1);return r;}
template<class S> Pent<S> lift(S x){Pent<S>r;r.c[0]=x;return r;}
template<class S> Pent<S> operator+(const Pent<S>&a,const Pent<S>&b){Pent<S>r;for(unsigned i=0;i<5;++i)r.c[i]=a.c[i]+b.c[i];return r;}
template<class S> Pent<S> operator-(const Pent<S>&a,const Pent<S>&b){Pent<S>r;for(unsigned i=0;i<5;++i)r.c[i]=a.c[i]-b.c[i];return r;}
template<class S> Pent<S> operator-(const Pent<S>&a){Pent<S>r;for(unsigned i=0;i<5;++i)r.c[i]=-a.c[i];return r;}
template<class S> Pent<S> operator*(const Pent<S>&a,const S&b){Pent<S>r;for(unsigned i=0;i<5;++i)r.c[i]=a.c[i]*b;return r;}
template<class S> Pent<S> operator*(const S&a,const Pent<S>&b){return b*a;}
template<class S> Pent<S> divide(const Pent<S>&a,const S&b){Pent<S>r;for(unsigned i=0;i<5;++i)r.c[i]=divide(a.c[i],b);return r;}
// Exact normalized A_gamma_v1 table, in ordered coefficient coordinates.
// Gamma is a runtime parameter; all five coordinates remain present at zero.
template<class S> Pent<S> product(const Pent<S>&a,const Pent<S>&b,double gamma){
    const double k=1.0/std::sqrt(1.0+gamma*gamma),s=gamma*k,h=1.0/std::sqrt(2.0);
    Pent<S>r;
    for(unsigned i=0;i<5;++i){r.c[i]=a.c[0]*b.c[i];if(i)r.c[i]=r.c[i]+a.c[i]*b.c[0];}
    for(unsigned i=1;i<5;++i)r.c[0]=r.c[0]-a.c[i]*b.c[i];
    auto cross=[&](unsigned i,unsigned j,unsigned coordinate,long double coefficient){
        r.c[coordinate]=r.c[coordinate]+(a.c[i]*b.c[j]-a.c[j]*b.c[i])*S(coefficient);
    };
    cross(1,2,3,k);cross(1,2,4,s);cross(1,3,2,-k);cross(1,3,4,-s);
    cross(1,4,2,-h);cross(1,4,3,-h);cross(2,3,1,k);cross(2,3,4,s);
    cross(2,4,1,-h);cross(2,4,3,-h);cross(3,4,1,-h);cross(3,4,2,-h);
    return r;
}
template<class S> Pent<S> power(Pent<S>a,unsigned exponent,double gamma){Pent<S>r=lift(S(1));while(exponent){if(exponent&1u)r=product(r,a,gamma);exponent>>=1u;if(exponent)a=product(a,a,gamma);}return r;}
template<class S> Pent<S> conjugate(Pent<S>a){for(unsigned i=1;i<5;++i)a.c[i]=-a.c[i];return a;}
template<class S> Pent<S> pure(Pent<S>a){a.c[0]=S(0);return a;}
template<class S> S norm2(const Pent<S>&a){S r(0);for(const auto&x:a.c)r=r+x*x;return r;}
template<class S> Pent<S> commutator(const Pent<S>&a,const Pent<S>&b,double g){return product(a,b,g)-product(b,a,g);}
template<class S> Pent<S> associator(const Pent<S>&a,const Pent<S>&b,const Pent<S>&c,double g){return product(product(a,b,g),c,g)-product(a,product(b,c,g),g);}
inline long double math_sin(long double x){return checked(std::sin(x));}
inline long double math_cos(long double x){return checked(std::cos(x));}
inline long double math_tan(long double x){return checked(std::tan(x));}
inline long double math_exp(long double x){return checked(std::exp(x));}
inline long double math_log(long double x){if(x<=0)throw std::domain_error("log requires positive real input.");return checked(std::log(x));}
inline long double math_sqrt(long double x){if(x<0)throw std::domain_error("sqrt requires nonnegative real input.");return checked(std::sqrt(x));}
inline long double math_sinh(long double x){return checked(std::sinh(x));}
inline long double math_cosh(long double x){return checked(std::cosh(x));}
inline long double math_tanh(long double x){return checked(std::tanh(x));}
template<std::size_t N,class F> Dual<N> unary(const Dual<N>&x,long double y,F derivative){
    Dual<N>r(checked(y));r.stateDependent=x.stateDependent;const long double slope=checked(derivative());
    for(std::size_t i=0;i<N;++i)r.d[i]=checked(slope*x.d[i]);return r;
}
template<std::size_t N> Dual<N> math_sin(const Dual<N>&x){return unary(x,std::sin(x.v),[&]{return std::cos(x.v);});}
template<std::size_t N> Dual<N> math_cos(const Dual<N>&x){return unary(x,std::cos(x.v),[&]{return -std::sin(x.v);});}
template<std::size_t N> Dual<N> math_tan(const Dual<N>&x){return unary(x,std::tan(x.v),[&]{const auto c=std::cos(x.v);return 1/(c*c);});}
template<std::size_t N> Dual<N> math_exp(const Dual<N>&x){const auto y=math_exp(x.v);return unary(x,y,[&]{return y;});}
template<std::size_t N> Dual<N> math_log(const Dual<N>&x){const auto y=math_log(x.v);return unary(x,y,[&]{return 1/x.v;});}
template<std::size_t N> Dual<N> math_sqrt(const Dual<N>&x){
    const auto y=math_sqrt(x.v);
    if(y==0){
        // Vanishing first tangents do not establish smoothness: sqrt(x*x) at
        // zero is |x|. Conservatively reject any state-dependent zero root,
        // including identities whose local smoothness this AD cannot prove.
        if(x.stateDependent)throw std::domain_error("Cannot certify a state Jacobian for sqrt of a state-dependent zero.");
        return Dual<N>(0);
    }
    return unary(x,y,[&]{return 0.5L/y;});
}
template<std::size_t N> Dual<N> math_sinh(const Dual<N>&x){return unary(x,math_sinh(x.v),[&]{return std::cosh(x.v);});}
template<std::size_t N> Dual<N> math_cosh(const Dual<N>&x){return unary(x,math_cosh(x.v),[&]{return std::sinh(x.v);});}
template<std::size_t N> Dual<N> math_tanh(const Dual<N>&x){const auto y=math_tanh(x.v);return unary(x,y,[&]{const auto e=std::exp(-2*std::abs(x.v));return 4*e/((1+e)*(1+e));});}
inline double binary64(long double x){if(!std::isfinite(x)||std::abs(x)>std::numeric_limits<double>::max())throw std::domain_error("Native ODE output exceeds finite binary64.");return static_cast<double>(x);}


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


#include <vector>
namespace penta_reference {
struct UniversalTerm {unsigned left,right,output;long double coefficient;};
template<unsigned Tag>struct UniversalTraits;
template<class S,unsigned Tag>struct Universal {
    std::array<S,UniversalTraits<Tag>::dimension> c{};
};
template<class S,unsigned T>Universal<S,T> universal_load(const S* values){Universal<S,T> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=values[k];return r;}
template<class S,unsigned T>Universal<S,T> universal_basis(unsigned index){Universal<S,T> r;r.c.at(index)=S(1);return r;}
template<class S,unsigned T>Universal<S,T> universal_lift(const S& scalar){
    if(!UniversalTraits<T>::has_unit)throw std::domain_error("Real scalar promotion requires an algebra unit.");
    Universal<S,T> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=S(UniversalTraits<T>::unit[k])*scalar;return r;
}
template<class S,unsigned T>Universal<S,T> operator+(const Universal<S,T>& a,const Universal<S,T>& b){Universal<S,T> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=a.c[k]+b.c[k];return r;}
template<class S,unsigned T>Universal<S,T> operator-(const Universal<S,T>& a,const Universal<S,T>& b){Universal<S,T> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=a.c[k]-b.c[k];return r;}
template<class S,unsigned T>Universal<S,T> operator-(const Universal<S,T>& a){Universal<S,T> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=-a.c[k];return r;}
template<class S,unsigned T>Universal<S,T> operator+(const Universal<S,T>& a){return a;}
template<class S,unsigned T>Universal<S,T> operator*(const Universal<S,T>& a,const S& b){Universal<S,T> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=a.c[k]*b;return r;}
template<class S,unsigned T>Universal<S,T> operator*(const S& a,const Universal<S,T>& b){return b*a;}
template<class S,unsigned T>Universal<S,T> divide(const Universal<S,T>& a,const S& b){Universal<S,T> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=divide(a.c[k],b);return r;}
template<class S,unsigned T>Universal<S,T> universal_product(const Universal<S,T>& a,const Universal<S,T>& b){
    Universal<S,T> r;for(const auto& term:UniversalTraits<T>::terms)r.c[term.output]=r.c[term.output]+(a.c[term.left]*b.c[term.right])*S(term.coefficient);return r;
}
template<class S,unsigned T>Universal<S,T> universal_conjugate(const Universal<S,T>& a){
    if(!UniversalTraits<T>::has_conjugation)throw std::domain_error("This algebra has no declared conjugation.");
    Universal<S,T> r;for(unsigned row=0;row<r.c.size();++row)for(unsigned column=0;column<r.c.size();++column){const auto coefficient=UniversalTraits<T>::conjugation[row*r.c.size()+column];if(coefficient!=0)r.c[row]=r.c[row]+a.c[column]*S(coefficient);}return r;
}
template<class S,unsigned T>S universal_coordinate_norm2(const Universal<S,T>& a){S r(0);for(const auto& coefficient:a.c)r=r+coefficient*coefficient;return r;}
template<class S,unsigned T>S universal_norm2(const Universal<S,T>& a){
    if(!UniversalTraits<T>::has_metric)throw std::domain_error("This algebra has no declared metric.");
    S r(0);for(unsigned row=0;row<a.c.size();++row)for(unsigned column=0;column<a.c.size();++column){const auto coefficient=UniversalTraits<T>::metric[row*a.c.size()+column];if(coefficient!=0)r=r+(a.c[row]*a.c[column])*S(coefficient);}return r;
}
template<class S,unsigned T>Universal<S,T> universal_pure(Universal<S,T> a){if(!UniversalTraits<T>::scalar_first)throw std::domain_error("No scalar coordinate functional is declared for this basis.");a.c[0]=S(0);return a;}
template<class S,unsigned T>bool universal_equal(const Universal<S,T>& a,const Universal<S,T>& b){bool equal=true;for(unsigned k=0;k<a.c.size();++k)equal=(a.c[k]==b.c[k])&&equal;return equal;}
template<class S,unsigned T>Universal<S,T> universal_inverse(const Universal<S,T>& a){
    if(!UniversalTraits<T>::has_unit)throw std::domain_error("An inverse requires an algebra unit.");
    // Every nonzero standard Cayley-Dickson element has this reciprocal. For
    // sedenions it does NOT imply invertible left/right multiplication maps.
    if constexpr(UniversalTraits<T>::canonical_reciprocal)return divide(universal_conjugate(a),universal_coordinate_norm2(a));
    else {
        if(!UniversalTraits<T>::associative_exact)throw std::domain_error("Native inverse derivatives require a certified associative algebra or a defined canonical reciprocal.");
        constexpr unsigned n=UniversalTraits<T>::dimension;
        std::vector<S> matrix(n*n),rhs(n);std::vector<long double> inverse(n*n,0);for(unsigned i=0;i<n;++i){rhs[i]=S(UniversalTraits<T>::unit[i]);inverse[i*n+i]=1;}
        for(const auto& term:UniversalTraits<T>::terms)matrix[term.output*n+term.right]=matrix[term.output*n+term.right]+a.c[term.left]*S(term.coefficient);
        long double scale=0,matrixNorm=0;for(const auto& entry:matrix)scale=std::max(scale,std::abs(value(entry)));for(unsigned column=0;column<n;++column){long double sum=0;for(unsigned row=0;row<n;++row)sum+=std::abs(value(matrix[row*n+column]));matrixNorm=std::max(matrixNorm,sum);}
        if(scale==0)throw std::domain_error("Singular native algebra inverse.");
        for(unsigned column=0;column<n;++column){unsigned pivot=column;for(unsigned row=column+1;row<n;++row)if(std::abs(value(matrix[row*n+column]))>std::abs(value(matrix[pivot*n+column])))pivot=row;
            if(std::abs(value(matrix[pivot*n+column]))<=1e-14L*scale)throw std::domain_error("Singular or ill-conditioned native algebra inverse.");
            if(pivot!=column){for(unsigned j=0;j<n;++j){std::swap(matrix[pivot*n+j],matrix[column*n+j]);std::swap(inverse[pivot*n+j],inverse[column*n+j]);}std::swap(rhs[pivot],rhs[column]);}
            const auto diagonal=matrix[column*n+column];for(unsigned j=0;j<n;++j)inverse[column*n+j]/=value(diagonal);for(unsigned j=column;j<n;++j)matrix[column*n+j]=divide(matrix[column*n+j],diagonal);rhs[column]=divide(rhs[column],diagonal);
            for(unsigned row=0;row<n;++row)if(row!=column){const auto factor=matrix[row*n+column];for(unsigned j=0;j<n;++j)inverse[row*n+j]-=value(factor)*inverse[column*n+j];for(unsigned j=column;j<n;++j)matrix[row*n+j]=matrix[row*n+j]-factor*matrix[column*n+j];rhs[row]=rhs[row]-factor*rhs[column];}
        }
        long double inverseNorm=0;for(unsigned column=0;column<n;++column){long double sum=0;for(unsigned row=0;row<n;++row)sum+=std::abs(inverse[row*n+column]);inverseNorm=std::max(inverseNorm,sum);}
        // cond2(L) <= n*cond1(L). This conservative bound preserves the core's
        // 1e12 conditioning limit without embedding a numerical library in DLLs.
        if(!std::isfinite(inverseNorm)||matrixNorm*inverseNorm>1e12L/n)throw std::domain_error("Native inverse rejected by its conservative conditioning bound.");
        Universal<S,T> result;for(unsigned k=0;k<n;++k)result.c[k]=rhs[k];
        const auto left=universal_product(a,result),right=universal_product(result,a);long double residual=0,normal=1;
        for(unsigned k=0;k<n;++k){residual=std::max({residual,std::abs(value(left.c[k])-UniversalTraits<T>::unit[k]),std::abs(value(right.c[k])-UniversalTraits<T>::unit[k])});normal=std::max(normal,std::abs(value(a.c[k]))*std::abs(value(result.c[k])));}
        if(residual>1e-10L*normal)throw std::domain_error("Native inverse failed its two-sided residual check.");return result;
    }
}
template<class S,unsigned T>Universal<S,T> universal_power(Universal<S,T> a,long long exponent){
    if(exponent<-64||exponent>64)throw std::domain_error("Native algebra powers are bounded to[-64,64].");
    if(exponent==0)return universal_lift<S,T>(S(1));if(exponent<0){a=universal_inverse(a);exponent=-exponent;}
    // The explicit left fold matches the general algebra API without assuming
    // associativity or reassociating a user expression tree.
    auto result=a;for(long long k=1;k<exponent;++k)result=universal_product(result,a);return result;
}
template<class S>S universal_even(const S& w,bool hyper,bool sinc){
    if(std::abs(value(w))<1){S result(0),power(1);long double coefficient=1;for(unsigned k=0;k<32;++k){if(k){const unsigned a=sinc?2*k:2*k-1,b=sinc?2*k+1:2*k;coefficient*=(hyper?1.L:-1.L)/(static_cast<long double>(a)*b);power=power*w;}result=result+power*S(coefficient);}return result;}
    const bool negative=value(w)<0;const auto root=cb_sqrt(negative?-w:w);const bool actualHyper=negative?!hyper:hyper;
    return sinc?divide(actualHyper?cb_sinh(root):cb_sin(root),root):(actualHyper?cb_cosh(root):cb_cos(root));
}
template<class S,unsigned T>Universal<S,T> universal_whole(const Universal<S,T>& a,unsigned operation){
    if constexpr(UniversalTraits<T>::quadratic){
        S w(0);for(unsigned k=1;k<a.c.size();++k)w=w+a.c[k]*a.c[k]*S(UniversalTraits<T>::pure_square[k]);
        Universal<S,T> result;S amplitude;
        if(operation==1){result.c[0]=cb_sin(a.c[0])*universal_even(w,true,false);amplitude=cb_cos(a.c[0])*universal_even(w,true,true);}
        else if(operation==2){result.c[0]=cb_cos(a.c[0])*universal_even(w,true,false);amplitude=-cb_sin(a.c[0])*universal_even(w,true,true);}
        else if(operation==3){
            if(value(w)<-1){
                // Hyperbolic idempotent exponents avoid exp(a0)*cosh(root),
                // whose separate factors can overflow and underflow although
                // every final coefficient and derivative is representable.
                const auto root=cb_sqrt(-w),plus=cb_exp(a.c[0]+root)*S(.5L),minus=cb_exp(a.c[0]-root)*S(.5L);
                result.c[0]=plus+minus;amplitude=divide(plus-minus,root);
            }else{const auto e=cb_exp(a.c[0]);result.c[0]=e*universal_even(w,false,false);amplitude=e*universal_even(w,false,true);}
        }
        else if(operation==4){result.c[0]=cb_sinh(a.c[0])*universal_even(w,false,false);amplitude=cb_cosh(a.c[0])*universal_even(w,false,true);}
        else{result.c[0]=cb_cosh(a.c[0])*universal_even(w,false,false);amplitude=cb_sinh(a.c[0])*universal_even(w,false,true);}
        for(unsigned k=1;k<a.c.size();++k)result.c[k]=amplitude*a.c[k];return result;
    } else {
        if(!UniversalTraits<T>::power_associative_exact||!UniversalTraits<T>::has_unit)throw std::domain_error("No native one-element calculus is certified for this algebra.");
        long double magnitude=0;for(const auto& coefficient:a.c)magnitude+=std::abs(value(coefficient));magnitude*=UniversalTraits<T>::product_bound;
        unsigned scaling=0;while(magnitude>.5L){magnitude*=.5L;if(++scaling>32)throw std::domain_error("General native elementary function exceeds its supported scaling range.");}
        const auto x=divide(a,S(std::ldexp(1.L,static_cast<int>(scaling))));const auto one=universal_lift<S,T>(S(1));
        if(operation==3){auto result=one,term=one;for(unsigned k=1;k<=64;++k){term=divide(universal_product(term,x),S(k));result=result+term;}for(unsigned k=0;k<scaling;++k)result=universal_product(result,result);return result;}
        const bool hyper=operation==4||operation==5;auto even=one,odd=x,term=one;
        for(unsigned k=1;k<=64;++k){term=divide(universal_product(term,x),S(k));const long double sign=hyper||((k/2)%2==0)?1.L:-1.L;if(k%2)odd= k==1?term:odd+term*S(sign);else even=even+term*S(sign);}
        for(unsigned k=0;k<scaling;++k){const auto nextOdd=universal_product(odd,even)+universal_product(even,odd);const auto nextEven=hyper?universal_product(even,even)+universal_product(odd,odd):universal_product(even,even)-universal_product(odd,odd);odd=nextOdd;even=nextEven;}
        return operation==1||operation==4?odd:even;
    }
}
template<class S,unsigned T>Universal<S,T> universal_complex_log(const Universal<S,T>& a){static_assert(UniversalTraits<T>::dimension==2);Universal<S,T> result;result.c[0]=cb_log(cb_hypot(a.c[0],a.c[1]));result.c[1]=cb_atan2(a.c[1],a.c[0]);return result;}
}


namespace penta_reference {
// Coefficients cross the existing real C ABI in row-major cell order, followed
// by the complete element coefficient order. MATLAB linear indexing is separate.
template<int Tag>constexpr unsigned fixed_array_width(){if constexpr(Tag>=0)return UniversalTraits<Tag>::dimension;else return Tag==-2?5:1;}
template<class S,unsigned R,unsigned C,int Tag>struct FixedArray {
    static constexpr unsigned rows=R,columns=C,components=fixed_array_width<Tag>();
    static constexpr int tag=Tag;
    std::array<S,R*C*components> c{};
};
template<class S,unsigned R,unsigned C,int Tag>auto array_element(const FixedArray<S,R,C,Tag>& a,unsigned index){
    if(index>=R*C)throw std::domain_error("Native array cell index exceeds its fixed shape.");
    if constexpr(Tag==-1)return a.c[index];
    else if constexpr(Tag==-2){Pent<S> value{};for(unsigned k=0;k<5;++k)value.c[k]=a.c[index*5+k];return value;}
    else{Universal<S,Tag> value{};for(unsigned k=0;k<value.c.size();++k)value.c[k]=a.c[index*value.c.size()+k];return value;}
}
template<class S,unsigned R,unsigned C,int Tag,class E>void array_put(FixedArray<S,R,C,Tag>& a,unsigned index,const E& value){
    if(index>=R*C)throw std::domain_error("Native array cell index exceeds its fixed shape.");
    if constexpr(Tag==-1)a.c[index]=value;
    else for(unsigned k=0;k<fixed_array_width<Tag>();++k)a.c[index*fixed_array_width<Tag>()+k]=value.c[k];
}
template<int Tag,class S>auto array_scalar(const S& value){if constexpr(Tag==-1)return value;else if constexpr(Tag==-2)return lift<S>(value);else return universal_lift<S,Tag>(value);}
template<class S,unsigned R,unsigned C,int Tag>FixedArray<S,R,C,Tag> array_load(const S* values){FixedArray<S,R,C,Tag> out{};std::copy_n(values,out.c.size(),out.c.begin());return out;}
template<class S,unsigned R,unsigned C,int Tag,class... E>FixedArray<S,R,C,Tag> array_make(const E&... values){
    static_assert(sizeof...(E)==R*C);FixedArray<S,R,C,Tag> out{};unsigned n=0;(array_put(out,n++,values),...);return out;
}
template<int Target,class S,unsigned R,unsigned C,int Source>auto array_cast(const FixedArray<S,R,C,Source>& a){
    FixedArray<S,R,C,Target> out{};
    for(unsigned n=0;n<R*C;++n){if constexpr(Target==Source)array_put(out,n,array_element(a,n));else {static_assert(Source==-1,"Only real entries embed into another native algebra.");array_put(out,n,array_scalar<Target>(array_element(a,n)));}}
    return out;
}
template<class S,unsigned R,unsigned C,int Tag>auto operator+(const FixedArray<S,R,C,Tag>& a){return a;}
template<class S,unsigned R,unsigned C,int Tag>auto operator-(const FixedArray<S,R,C,Tag>& a){auto out=a;for(auto& x:out.c)x=-x;return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator+(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b){auto out=a;for(unsigned k=0;k<out.c.size();++k)out.c[k]=a.c[k]+b.c[k];return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator-(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b){auto out=a;for(unsigned k=0;k<out.c.size();++k)out.c[k]=a.c[k]-b.c[k];return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator*(const FixedArray<S,R,C,Tag>& a,const S& b){auto out=a;for(auto& x:out.c)x=x*b;return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator*(const S& a,const FixedArray<S,R,C,Tag>& b){return b*a;}
template<class S,unsigned R,unsigned C,int Tag>auto array_divide(const FixedArray<S,R,C,Tag>& a,const S& b){auto out=a;for(auto& x:out.c)x=divide(x,b);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_add_scalar(const FixedArray<S,R,C,Tag>& a,const S& b,bool subtract=false){auto out=a;const auto scalar=array_scalar<Tag>(b);for(unsigned n=0;n<R*C;++n)array_put(out,n,subtract?array_element(a,n)-scalar:array_element(a,n)+scalar);return out;}
template<int Tag,class E>auto array_product(const E& a,const E& b,double gamma){if constexpr(Tag==-1)return a*b;else if constexpr(Tag==-2)return product(a,b,gamma);else return universal_product(a,b);}
template<class S,unsigned R,unsigned K,unsigned C,int Tag>auto array_matmul(const FixedArray<S,R,K,Tag>& a,const FixedArray<S,K,C,Tag>& b,double gamma){
    FixedArray<S,R,C,Tag> out{};for(unsigned row=0;row<R;++row)for(unsigned column=0;column<C;++column){auto sum=array_element(out,row*C+column);for(unsigned inner=0;inner<K;++inner)sum=sum+array_product<Tag>(array_element(a,row*K+inner),array_element(b,inner*C+column),gamma);array_put(out,row*C+column,sum);}return out;
}
template<class S,unsigned R,unsigned C,int Tag>auto array_entry_product(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b,double gamma){auto out=a;for(unsigned n=0;n<R*C;++n)array_put(out,n,array_product<Tag>(array_element(a,n),array_element(b,n),gamma));return out;}
template<class S,unsigned R,unsigned C,int Tag,class E>auto array_add_element(const FixedArray<S,R,C,Tag>& a,const E& b,bool subtract=false){auto out=a;for(unsigned n=0;n<R*C;++n)array_put(out,n,subtract?array_element(a,n)-b:array_element(a,n)+b);return out;}
template<class S,unsigned R,unsigned C,int Tag,class E>auto array_scale_element(const FixedArray<S,R,C,Tag>& a,const E& b,double gamma,bool left=false){auto out=a;for(unsigned n=0;n<R*C;++n)array_put(out,n,left?array_product<Tag>(b,array_element(a,n),gamma):array_product<Tag>(array_element(a,n),b,gamma));return out;}
template<class S,unsigned N,int Tag>auto array_power(const FixedArray<S,N,N,Tag>& a,unsigned exponent,double gamma){
    if(exponent>64)throw std::domain_error("Native matrix powers are bounded to0..64.");
    if(!exponent){FixedArray<S,N,N,Tag> out{};for(unsigned n=0;n<N;++n)array_put(out,n*N+n,array_scalar<Tag>(S(1)));return out;}
    auto out=a;for(unsigned n=1;n<exponent;++n)out=array_matmul(out,a,gamma);return out;
}
template<class S,unsigned R,unsigned C,int Tag>auto array_entry_divide(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,-1>& b){auto out=a;for(unsigned n=0;n<R*C;++n){const auto denominator=array_element(b,n);for(unsigned k=0;k<out.components;++k)out.c[n*out.components+k]=divide(a.c[n*out.components+k],denominator);}return out;}
template<class E>auto array_conjugate(const E& value){return value;}
template<class S>auto array_conjugate(const Pent<S>& value){return conjugate(value);}
template<class S,unsigned Tag>auto array_conjugate(const Universal<S,Tag>& value){return universal_conjugate(value);}
template<class S,unsigned R,unsigned C,int Tag>auto array_transpose(const FixedArray<S,R,C,Tag>& a,bool conjugate=false){FixedArray<S,C,R,Tag> out{};for(unsigned row=0;row<R;++row)for(unsigned column=0;column<C;++column){const auto value=array_element(a,row*C+column);array_put(out,column*R+row,conjugate?array_conjugate(value):value);}return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_at(const FixedArray<S,R,C,Tag>& a,unsigned row,unsigned column){if(row>=R||column>=C)throw std::domain_error("Native array index exceeds its fixed shape.");return array_element(a,row*C+column);}
template<class S,unsigned R,unsigned C,int Tag>auto array_linear_at(const FixedArray<S,R,C,Tag>& a,unsigned oneBased){if(oneBased<1||oneBased>R*C)throw std::domain_error("Native array linear index exceeds its fixed shape.");const unsigned n=oneBased-1;return array_at(a,n%R,n/R);}
template<int Target,class S,unsigned R,unsigned C,int Tag,class Function>auto array_map_as(const FixedArray<S,R,C,Tag>& a,Function function){FixedArray<S,R,C,Target> out{};for(unsigned n=0;n<R*C;++n)array_put(out,n,function(array_element(a,n)));return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_sum(const FixedArray<S,R,C,Tag>& a){auto zero=a;for(auto& x:zero.c)x=S(0);auto sum=array_element(zero,0);for(unsigned n=0;n<R*C;++n)sum=sum+array_element(a,n);return sum;}
template<unsigned Axis,class S,unsigned R,unsigned C,int Tag>auto array_reduce(const FixedArray<S,R,C,Tag>& a,bool mean=false){
    if constexpr(Axis==1){FixedArray<S,1,C,Tag> out{};for(unsigned column=0;column<C;++column){auto sum=array_element(out,column);for(unsigned row=0;row<R;++row)sum=sum+array_element(a,row*C+column);array_put(out,column,sum);}return mean?array_divide(out,S(R)):out;}
    else if constexpr(Axis==2){FixedArray<S,R,1,Tag> out{};for(unsigned row=0;row<R;++row){auto sum=array_element(out,row);for(unsigned column=0;column<C;++column)sum=sum+array_element(a,row*C+column);array_put(out,row,sum);}return mean?array_divide(out,S(C)):out;}
    else return a;
}
template<class S,unsigned R,unsigned C,int Tag>auto array_norm(const FixedArray<S,R,C,Tag>& a){S sum(0);for(const auto& x:a.c)sum=sum+x*x;return cb_sqrt(sum);}
template<class S,unsigned R,unsigned C,int Tag>bool array_equal(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b){for(unsigned k=0;k<a.c.size();++k)if(!(a.c[k]==b.c[k]))return false;return true;}
}


namespace penta_reference {
// Tensor elements use MATLAB column-major ordering across every axis. Every
// element retains its adjacent complete algebra coefficients. FixedArray's
// row-major ABI and all existing generated sources remain unchanged.
template<class S,int Tag,unsigned... D>struct TypedTensor {
    static constexpr unsigned rank=sizeof...(D),elements=(1u*...*D),components=fixed_array_width<Tag>();
    static constexpr int tag=Tag;
    static constexpr std::array<unsigned,rank> shape{{D...}};
    std::array<S,elements*components> c{};
};
template<class S,int Tag,unsigned... D>auto tensor_element(const TypedTensor<S,Tag,D...>& a,unsigned index){
    if(index>=a.elements)throw std::domain_error("Native tensor index exceeds its immutable shape.");
    if constexpr(Tag==-1)return a.c[index];
    else if constexpr(Tag==-2){Pent<S> value{};for(unsigned k=0;k<5;++k)value.c[k]=a.c[index*5+k];return value;}
    else{Universal<S,Tag> value{};for(unsigned k=0;k<value.c.size();++k)value.c[k]=a.c[index*value.c.size()+k];return value;}
}
template<class S,int Tag,unsigned... D,class E>void tensor_put(TypedTensor<S,Tag,D...>& a,unsigned index,const E& value){
    if(index>=a.elements)throw std::domain_error("Native tensor assignment exceeds its immutable shape.");
    if constexpr(Tag==-1)a.c[index]=value;
    else for(unsigned k=0;k<a.components;++k)a.c[index*a.components+k]=value.c[k];
}
template<class S,int Tag,unsigned... D>auto tensor_load(const S* values){TypedTensor<S,Tag,D...> out{};std::copy_n(values,out.c.size(),out.c.begin());return out;}
template<class S,int Tag,unsigned... D,class E>auto tensor_fill(const E& value){TypedTensor<S,Tag,D...> out{};for(unsigned n=0;n<out.elements;++n)tensor_put(out,n,value);return out;}
template<class S,int Tag,unsigned... D,unsigned R,unsigned C,int Source>auto tensor_from_array(const FixedArray<S,R,C,Source>& a){
    TypedTensor<S,Tag,D...> out{};static_assert(R*C==out.elements||R*C==1,"Tensor reshape must retain element count.");
    static_assert(Tag==Source,"Tensor inference cannot change the complete entry algebra.");
    for(unsigned n=0;n<out.elements;++n){const auto cell=R*C==1?0:(n%R)*C+n/R;tensor_put(out,n,array_element(a,cell));}return out;
}
template<class S,int Tag,unsigned... D,unsigned R,unsigned C>auto tensor_from_coordinates(const FixedArray<S,R,C,-1>& a){
    TypedTensor<S,Tag,D...> out{};static_assert(C==out.components&&(R==1||R==out.elements),"Tensor coordinate rows must retain every coefficient.");
    for(unsigned n=0;n<out.elements;++n)for(unsigned k=0;k<out.components;++k)out.c[n*out.components+k]=a.c[(R==1?0:n)*C+k];return out;
}
template<unsigned... E,class S,int Tag,unsigned... D>auto tensor_reshape(const TypedTensor<S,Tag,D...>& a){
    TypedTensor<S,Tag,E...> out{};static_assert(out.elements==a.elements,"Tensor reshape must retain element count.");std::copy(a.c.begin(),a.c.end(),out.c.begin());return out;
}
template<class S,int Tag,unsigned... D>auto operator+(const TypedTensor<S,Tag,D...>& a){return a;}
template<class S,int Tag,unsigned... D>auto operator-(const TypedTensor<S,Tag,D...>& a){auto out=a;for(auto& x:out.c)x=-x;return out;}
template<class S,int Tag,unsigned... D>auto operator+(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b){auto out=a;for(unsigned n=0;n<out.c.size();++n)out.c[n]=a.c[n]+b.c[n];return out;}
template<class S,int Tag,unsigned... D>auto operator-(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b){auto out=a;for(unsigned n=0;n<out.c.size();++n)out.c[n]=a.c[n]-b.c[n];return out;}
template<class S,int Tag,unsigned... D,class E>auto tensor_add_element(const TypedTensor<S,Tag,D...>& a,const E& b,bool subtract=false){auto out=a;for(unsigned n=0;n<out.elements;++n)tensor_put(out,n,subtract?tensor_element(a,n)-b:tensor_element(a,n)+b);return out;}
template<class S,int Tag,unsigned... D>auto tensor_entry_product(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b,double gamma){auto out=a;for(unsigned n=0;n<out.elements;++n)tensor_put(out,n,array_product<Tag>(tensor_element(a,n),tensor_element(b,n),gamma));return out;}
template<class S,int Tag,unsigned... D>auto tensor_scale_real(const TypedTensor<S,Tag,D...>& a,const S& b){auto out=a;for(auto& value:out.c)value=value*b;return out;}
template<class S,int Tag,unsigned... D,class E>auto tensor_scale_element(const TypedTensor<S,Tag,D...>& a,const E& b,double gamma,bool left=false){auto out=a;for(unsigned n=0;n<out.elements;++n)tensor_put(out,n,left?array_product<Tag>(b,tensor_element(a,n),gamma):array_product<Tag>(tensor_element(a,n),b,gamma));return out;}
template<class S,int Tag,unsigned... D>auto tensor_divide_real(const TypedTensor<S,Tag,D...>& a,const S& b){auto out=a;for(auto& value:out.c)value=divide(value,b);return out;}
template<int Target,class S,int Tag,unsigned... D,class F>auto tensor_map_as(const TypedTensor<S,Tag,D...>& a,F function){TypedTensor<S,Target,D...> out{};for(unsigned n=0;n<out.elements;++n)tensor_put(out,n,function(tensor_element(a,n)));return out;}
template<class S,int Tag,unsigned... D>auto tensor_coefficient(const TypedTensor<S,Tag,D...>& a,unsigned index){if(index>=a.components)throw std::domain_error("Native tensor coefficient exceeds its entry algebra.");TypedTensor<S,-1,D...> out{};for(unsigned n=0;n<out.elements;++n)out.c[n]=a.c[n*a.components+index];return out;}
template<class S,int Tag,unsigned... D>auto tensor_coordinates(const TypedTensor<S,Tag,D...>& a){using T=TypedTensor<S,Tag,D...>;FixedArray<S,T::elements,T::components,-1> out{};std::copy(a.c.begin(),a.c.end(),out.c.begin());return out;}
template<class S,int Tag,unsigned... D>auto tensor_norm(const TypedTensor<S,Tag,D...>& a){S sum(0);for(const auto& value:a.c)sum=sum+value*value;return cb_sqrt(sum);}
template<class S,int Tag,unsigned... D>bool tensor_equal(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b){for(unsigned n=0;n<a.c.size();++n)if(!(a.c[n]==b.c[n]))return false;return true;}
}
