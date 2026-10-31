// PRIVATE BUILD INPUT. Never stage this implementation in the public SDK.
#include "pentarnion/native_base.hpp"
extern "C" unsigned penta_native_bridge_abi_v1(){return 1;}
extern "C" std::size_t penta_native_bridge_long_double_size_v1(){return sizeof(long double);}
namespace penta_generated {
Jet2::Jet2() = default;
Jet2::Jet2(long double a):v(checked(a)){}
Jet2::operator bool() const {
    if(v==0&&dependent)throw std::domain_error("Derivative at a state-dependent branch boundary is unsupported.");
    return v!=0;
}
Jet3::Jet3() = default;
Jet3::Jet3(long double a){c[0]=checked(a);}
Jet3::operator bool() const {
    if(c[0]==0&&dependent)throw std::domain_error("Third derivative at a state-dependent branch boundary is unsupported.");
    return c[0]!=0;
}

long double checked(long double x) {
    if(!std::isfinite(x))throw std::domain_error("Nonfinite native ODE expression.");
    return x;
}
template<std::size_t N> struct Dual {
    long double v=0;std::array<long double,N>d{};
    bool stateDependent=false;
    Dual()=default;Dual(long double x):v(x){}
};
long double value(long double x){return x;}
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
long double divide(long double a,long double b){if(b==0)throw std::domain_error("Division by zero in native ODE.");return checked(a/b);}
template<std::size_t N> Dual<N> divide(const Dual<N>&a,const Dual<N>&b){return a/b;}
template<class S> S integerPower(S a,unsigned exponent){S r(1);while(exponent){if(exponent&1u)r=r*a;exponent>>=1u;if(exponent)a=a*a;}return r;}
template<class S> Pent<S> pent(S a,S b,S c,S d,S e){return {{{a,b,c,d,e}}};}
template<class S> Pent<S> basis(unsigned i){Pent<S>r;r.c.at(i)=S(1);return r;}
template<class S> Pent<S> lift(S x){Pent<S>r;r.c[0]=x;return r;}
template<class S> Pent<S> operator+(const Pent<S>&a,const Pent<S>&b){Pent<S>r;for(unsigned i=0;i<5;++i)r.c[i]=a.c[i]+b.c[i];return r;}
template<class S> Pent<S> operator-(const Pent<S>&a,const Pent<S>&b){Pent<S>r;for(unsigned i=0;i<5;++i)r.c[i]=a.c[i]-b.c[i];return r;}
template<class S> Pent<S> operator-(const Pent<S>&a){Pent<S>r;for(unsigned i=0;i<5;++i)r.c[i]=-a.c[i];return r;}
template<class S> Pent<S> operator*(const Pent<S>&a,const S&b){Pent<S>r;for(unsigned i=0;i<5;++i)r.c[i]=a.c[i]*b;return r;}
template<class S> Pent<S> operator*(const S&a,const Pent<S>&b){return b*a;}
template<class S> Pent<S> divide(const Pent<S>&a,const S&b){Pent<S>r;for(unsigned i=0;i<5;++i)r.c[i]=divide(a.c[i],b);return r;}
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
long double math_sin(long double x){return checked(std::sin(x));}
long double math_cos(long double x){return checked(std::cos(x));}
long double math_tan(long double x){return checked(std::tan(x));}
long double math_exp(long double x){return checked(std::exp(x));}
long double math_log(long double x){if(x<=0)throw std::domain_error("log requires positive real input.");return checked(std::log(x));}
long double math_sqrt(long double x){if(x<0)throw std::domain_error("sqrt requires nonnegative real input.");return checked(std::sqrt(x));}
long double math_sinh(long double x){return checked(std::sinh(x));}
long double math_cosh(long double x){return checked(std::cosh(x));}
long double math_tanh(long double x){return checked(std::tanh(x));}
template<std::size_t N,class F> Dual<N> unary(const Dual<N>&x,long double y,F derivative){
    Dual<N>r(checked(y));r.stateDependent=x.stateDependent;const long double slope=checked(derivative());
    for(std::size_t i=0;i<N;++i){r.d[i]=checked(slope*x.d[i]);}return r;
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
double binary64(long double x){if(!std::isfinite(x)||std::abs(x)>std::numeric_limits<double>::max())throw std::domain_error("Native ODE output exceeds finite binary64.");return static_cast<double>(x);}
long double value(const Jet2&a){return a.v;}
Jet2 cb_unary(const Jet2&a,long double f,long double d,long double d2){Jet2 r(f);r.dependent=a.dependent;r.x=checked(d*a.x);r.y=checked(d*a.y);r.xy=checked(d*a.xy+d2*a.x*a.y);return r;}
Jet2 operator+(const Jet2&a,const Jet2&b){Jet2 r(a.v+b.v);r.dependent=a.dependent||b.dependent;r.x=checked(a.x+b.x);r.y=checked(a.y+b.y);r.xy=checked(a.xy+b.xy);return r;}
Jet2 operator-(const Jet2&a,const Jet2&b){Jet2 r(a.v-b.v);r.dependent=a.dependent||b.dependent;r.x=checked(a.x-b.x);r.y=checked(a.y-b.y);r.xy=checked(a.xy-b.xy);return r;}
Jet2 operator-(const Jet2&a){Jet2 r(-a.v);r.dependent=a.dependent;r.x=-a.x;r.y=-a.y;r.xy=-a.xy;return r;}
Jet2 operator+(const Jet2&a){return a;}
Jet2 operator*(const Jet2&a,const Jet2&b){Jet2 r(a.v*b.v);r.dependent=a.dependent||b.dependent;r.x=checked(a.x*b.v+a.v*b.x);r.y=checked(a.y*b.v+a.v*b.y);r.xy=checked(a.xy*b.v+a.x*b.y+a.y*b.x+a.v*b.xy);return r;}
Jet2 operator/(const Jet2&a,const Jet2&b){if(b.v==0)throw std::domain_error("Division by zero in compiled callback.");const long double inverse=1/b.v;return a*cb_unary(b,inverse,-inverse*inverse,2*inverse*inverse*inverse);}
Jet2 divide(const Jet2&a,const Jet2&b){return a/b;}
bool cb_compare(const Jet2&a,const Jet2&b,unsigned op){if(a.v==b.v&&(a.dependent||b.dependent))throw std::domain_error("Derivative at a state-dependent comparison boundary is unsupported.");switch(op){case 0:return a.v==b.v;case 1:return a.v!=b.v;case 2:return a.v<b.v;case 3:return a.v<=b.v;case 4:return a.v>b.v;default:return a.v>=b.v;}}
bool operator==(const Jet2&a,const Jet2&b){return cb_compare(a,b,0);}
bool operator!=(const Jet2&a,const Jet2&b){return cb_compare(a,b,1);}
bool operator<(const Jet2&a,const Jet2&b){return cb_compare(a,b,2);}
bool operator<=(const Jet2&a,const Jet2&b){return cb_compare(a,b,3);}
bool operator>(const Jet2&a,const Jet2&b){return cb_compare(a,b,4);}
bool operator>=(const Jet2&a,const Jet2&b){return cb_compare(a,b,5);}
bool operator!(const Jet2&a){return !static_cast<bool>(a);}
Jet2 cb_sin(const Jet2&a){return cb_unary(a,std::sin(a.v),std::cos(a.v),-std::sin(a.v));}
Jet2 cb_cos(const Jet2&a){return cb_unary(a,std::cos(a.v),-std::sin(a.v),-std::cos(a.v));}
Jet2 cb_exp(const Jet2&a){const auto e=checked(std::exp(a.v));return cb_unary(a,e,e,e);}
Jet2 cb_expm1(const Jet2&a){const auto e=checked(std::exp(a.v));return cb_unary(a,std::expm1(a.v),e,e);}
Jet2 cb_log(const Jet2&a){if(a.v<=0)throw std::domain_error("log requires positive input.");return cb_unary(a,std::log(a.v),1/a.v,-1/(a.v*a.v));}
Jet2 cb_log1p(const Jet2&a){if(a.v<=-1)throw std::domain_error("log1p requires input greater than -1.");const auto d=1/(1+a.v);return cb_unary(a,std::log1p(a.v),d,-d*d);}
Jet2 cb_log10(const Jet2&a){return cb_log(a)/Jet2(std::log(10.L));}
Jet2 cb_log2(const Jet2&a){return cb_log(a)/Jet2(std::log(2.L));}
Jet2 cb_sqrt(const Jet2&a){if(a.v<0)throw std::domain_error("sqrt requires nonnegative input.");if(a.v==0){if(a.dependent)throw std::domain_error("Cannot certify a derivative of state-dependent sqrt at zero.");return Jet2(0);}const auto root=std::sqrt(a.v);return cb_unary(a,root,.5L/root,-.25L/(a.v*root));}
Jet2 cb_sinh(const Jet2&a){return cb_unary(a,std::sinh(a.v),std::cosh(a.v),std::sinh(a.v));}
Jet2 cb_cosh(const Jet2&a){return cb_unary(a,std::cosh(a.v),std::sinh(a.v),std::cosh(a.v));}
Jet2 cb_tanh(const Jet2&a){const auto t=std::tanh(a.v),e=std::exp(-2*std::abs(a.v)),d=4*e/((1+e)*(1+e));return cb_unary(a,t,d,-2*t*d);}
Jet2 cb_tan(const Jet2&a){const auto t=std::tan(a.v),d=1+t*t;return cb_unary(a,t,d,2*t*d);}
Jet2 cb_asin(const Jet2&a){if(std::abs(a.v)>=1){if(a.dependent||std::abs(a.v)>1)throw std::domain_error("asin derivative requires input strictly inside [-1,1].");return Jet2(std::asin(a.v));}const auto q=1-a.v*a.v,r=std::sqrt(q);return cb_unary(a,std::asin(a.v),1/r,a.v/(q*r));}
Jet2 cb_acos(const Jet2&a){if(std::abs(a.v)>=1){if(a.dependent||std::abs(a.v)>1)throw std::domain_error("acos derivative requires input strictly inside [-1,1].");return Jet2(std::acos(a.v));}const auto q=1-a.v*a.v,r=std::sqrt(q);return cb_unary(a,std::acos(a.v),-1/r,-a.v/(q*r));}
Jet2 cb_atan(const Jet2&a){const auto q=1+a.v*a.v;return cb_unary(a,std::atan(a.v),1/q,-2*a.v/(q*q));}
Jet2 cb_asinh(const Jet2&a){const auto q=1+a.v*a.v,r=std::sqrt(q);return cb_unary(a,std::asinh(a.v),1/r,-a.v/(q*r));}
Jet2 cb_acosh(const Jet2&a){if(a.v<=1){if(a.dependent||a.v<1)throw std::domain_error("acosh derivative requires input greater than1.");return Jet2(0);}const auto q=a.v*a.v-1,r=std::sqrt(q);return cb_unary(a,std::acosh(a.v),1/r,-a.v/(q*r));}
Jet2 cb_atanh(const Jet2&a){if(std::abs(a.v)>=1)throw std::domain_error("atanh requires input strictly inside (-1,1).");const auto q=1-a.v*a.v;return cb_unary(a,std::atanh(a.v),1/q,2*a.v/(q*q));}
Jet2 cb_abs(const Jet2&a){if(a.v==0&&a.dependent)throw std::domain_error("abs is nondifferentiable at zero.");return a.v<0?-a:a;}
Jet2 cb_floor(const Jet2&a){if(a.dependent&&a.v==std::floor(a.v))throw std::domain_error("floor is discontinuous at an integer.");return Jet2(std::floor(a.v));}
Jet2 cb_ceil(const Jet2&a){if(a.dependent&&a.v==std::floor(a.v))throw std::domain_error("ceil is discontinuous at an integer.");return Jet2(std::ceil(a.v));}
Jet2 cb_round(const Jet2&a){if(a.dependent&&std::abs(a.v-std::trunc(a.v))==.5L)throw std::domain_error("round is discontinuous at a half-integer.");return Jet2(std::round(a.v));}
Jet2 cb_sign(const Jet2&a){if(a.dependent&&a.v==0)throw std::domain_error("sign is discontinuous at zero.");return Jet2(a.v>0?1:a.v<0?-1:0);}
Jet2 cb_erf(const Jet2&a){const auto d=2/std::sqrt(std::acos(-1.L))*std::exp(-a.v*a.v);return cb_unary(a,std::erf(a.v),d,-2*a.v*d);}
Jet2 cb_erfc(const Jet2&a){const auto d=-2/std::sqrt(std::acos(-1.L))*std::exp(-a.v*a.v);return cb_unary(a,std::erfc(a.v),d,-2*a.v*d);}
Jet2 cb_hypot(const Jet2&a,const Jet2&b){const auto scale=std::max(std::abs(a.v),std::abs(b.v));if(scale==0){if(a.dependent||b.dependent)throw std::domain_error("hypot is nondifferentiable at the origin.");return Jet2(0);}const auto x=a/Jet2(scale),y=b/Jet2(scale);return cb_sqrt(x*x+y*y)*Jet2(scale);}
Jet2 cb_atan2(const Jet2&y,const Jet2&x){const auto q=x.v*x.v+y.v*y.v;if(q==0)throw std::domain_error("atan2 is undefined at the origin.");if(x.v<0&&y.v==0&&(x.dependent||y.dependent))throw std::domain_error("atan2 derivative crosses its branch cut.");Jet2 r(std::atan2(y.v,x.v));r.dependent=x.dependent||y.dependent;const auto fx=-y.v/q,fy=x.v/q,fxx=2*x.v*y.v/(q*q),fyy=-fxx,fxy=(y.v*y.v-x.v*x.v)/(q*q);r.x=checked(fx*x.x+fy*y.x);r.y=checked(fx*x.y+fy*y.y);r.xy=checked(fx*x.xy+fy*y.xy+fxx*x.x*x.y+fyy*y.x*y.y+fxy*(x.x*y.y+y.x*x.y));return r;}
Jet2 cb_ipow(Jet2 a,long long n){if(n==0)return Jet2(1);const bool negative=n<0;unsigned long long power=negative?static_cast<unsigned long long>(-n):static_cast<unsigned long long>(n);Jet2 r(1);while(power){if(power&1u)r=r*a;power>>=1u;if(power)a=a*a;}return negative?Jet2(1)/r:r;}
template<class S>Pent<S> cb_inverse(const Pent<S>&a){return divide(conjugate(a),norm2(a));}
template<class S>Pent<S> cb_pent_power(Pent<S>a,long long n,double gamma){if(n<0){a=cb_inverse(a);n=-n;}return power(a,static_cast<unsigned>(n),gamma);}
Jet2 cb_even(const Jet2&w,bool hyper,bool sinc){
    if(w.v<0)throw std::domain_error("Invalid pure-part length.");
    long double f=0,d=0,d2=0;
    if(w.v<.01L){long double coefficient=1;const long double sign=hyper?1:-1;for(unsigned k=0;k<24;++k){if(k){const unsigned a=sinc?2*k:2*k-1,b=sinc?2*k+1:2*k;coefficient*=sign/(static_cast<long double>(a)*b);}f+=coefficient*std::pow(w.v,static_cast<int>(k));if(k)d+=k*coefficient*std::pow(w.v,static_cast<int>(k-1));if(k>1)d2+=k*(k-1)*coefficient*std::pow(w.v,static_cast<int>(k-2));}}
    else{const auto r=std::sqrt(w.v),c=hyper?std::cosh(r):std::cos(r),s=(hyper?std::sinh(r):std::sin(r))/r;if(sinc){f=s;d=(c-s)/(2*w.v);d2=((hyper?w.v:-w.v)*s-3*c+3*s)/(4*w.v*w.v);}else{f=c;d=(hyper?1:-1)*s/2;d2=(hyper?c-s:s-c)/(4*w.v);}}
    return cb_unary(w,f,d,d2);
}
Pent<Jet2> cb_whole(const Pent<Jet2>&p,unsigned op){Jet2 w(0);for(unsigned n=1;n<5;++n)w=w+p.c[n]*p.c[n];Pent<Jet2> r;Jet2 amplitude;
    if(op==1){r.c[0]=cb_sin(p.c[0])*cb_even(w,true,false);amplitude=cb_cos(p.c[0])*cb_even(w,true,true);}
    else if(op==2){r.c[0]=cb_cos(p.c[0])*cb_even(w,true,false);amplitude=-cb_sin(p.c[0])*cb_even(w,true,true);}
    else if(op==3){const auto e=cb_exp(p.c[0]);r.c[0]=e*cb_even(w,false,false);amplitude=e*cb_even(w,false,true);}
    else if(op==4){r.c[0]=cb_sinh(p.c[0])*cb_even(w,false,false);amplitude=cb_cosh(p.c[0])*cb_even(w,false,true);}
    else{r.c[0]=cb_cosh(p.c[0])*cb_even(w,false,false);amplitude=cb_sinh(p.c[0])*cb_even(w,false,true);}
    for(unsigned n=1;n<5;++n){r.c[n]=amplitude*p.c[n];}return r;
}
template<class S>bool pent_equal(const Pent<S>&a,const Pent<S>&b){bool equal=true;for(unsigned n=0;n<5;++n)equal=(a.c[n]==b.c[n])&&equal;return equal;}
long double value(const Jet3& a){return a.c[0];}
Jet3 cb_unary(const Jet3& a,long double f,long double d,long double d2,long double d3){
    Jet3 r(f);r.dependent=a.dependent;if(!a.dependent)return r;
    for(const unsigned bit:{1u,2u,4u})r.c[bit]=checked(d*a.c[bit]);
    for(const unsigned mask:{3u,5u,6u}){const unsigned first=mask&(~mask+1u),second=mask^first;r.c[mask]=checked(d*a.c[mask]+d2*a.c[first]*a.c[second]);}
    r.c[7]=checked(d*a.c[7]+d2*(a.c[1]*a.c[6]+a.c[2]*a.c[5]+a.c[4]*a.c[3])+d3*a.c[1]*a.c[2]*a.c[4]);return r;
}
Jet3 operator+(const Jet3& a,const Jet3& b){Jet3 r;r.dependent=a.dependent||b.dependent;for(unsigned mask=0;mask<8;++mask)r.c[mask]=checked(a.c[mask]+b.c[mask]);return r;}
Jet3 operator-(const Jet3& a,const Jet3& b){Jet3 r;r.dependent=a.dependent||b.dependent;for(unsigned mask=0;mask<8;++mask)r.c[mask]=checked(a.c[mask]-b.c[mask]);return r;}
Jet3 operator-(const Jet3& a){Jet3 r;r.dependent=a.dependent;for(unsigned mask=0;mask<8;++mask)r.c[mask]=-a.c[mask];return r;}
Jet3 operator+(const Jet3& a){return a;}
Jet3 operator*(const Jet3& a,const Jet3& b){Jet3 r;r.dependent=a.dependent||b.dependent;for(unsigned mask=0;mask<8;++mask){long double sum=0;unsigned subset=mask;for(;;){sum=checked(sum+a.c[subset]*b.c[mask^subset]);if(subset==0)break;subset=(subset-1u)&mask;}r.c[mask]=sum;}return r;}
Jet3 operator/(const Jet3& a,const Jet3& b){if(b.c[0]==0)throw std::domain_error("Division by zero in third-order callback.");const auto inverse=1/b.c[0];return a*cb_unary(b,inverse,-inverse*inverse,2*inverse*inverse*inverse,-6*inverse*inverse*inverse*inverse);}
Jet3 divide(const Jet3& a,const Jet3& b){return a/b;}
bool cb_compare(const Jet3& a,const Jet3& b,unsigned operation){if(a.c[0]==b.c[0]&&(a.dependent||b.dependent))throw std::domain_error("Third derivative at a state-dependent comparison boundary is unsupported.");switch(operation){case 0:return a.c[0]==b.c[0];case 1:return a.c[0]!=b.c[0];case 2:return a.c[0]<b.c[0];case 3:return a.c[0]<=b.c[0];case 4:return a.c[0]>b.c[0];default:return a.c[0]>=b.c[0];}}
bool operator==(const Jet3& a,const Jet3& b){return cb_compare(a,b,0);}
bool operator!=(const Jet3& a,const Jet3& b){return cb_compare(a,b,1);}
bool operator<(const Jet3& a,const Jet3& b){return cb_compare(a,b,2);}
bool operator<=(const Jet3& a,const Jet3& b){return cb_compare(a,b,3);}
bool operator>(const Jet3& a,const Jet3& b){return cb_compare(a,b,4);}
bool operator>=(const Jet3& a,const Jet3& b){return cb_compare(a,b,5);}
bool operator!(const Jet3& a){return !static_cast<bool>(a);}
Jet3 cb_sin(const Jet3& a){const auto s=std::sin(a.c[0]),c=std::cos(a.c[0]);return cb_unary(a,s,c,-s,-c);}
Jet3 cb_cos(const Jet3& a){const auto s=std::sin(a.c[0]),c=std::cos(a.c[0]);return cb_unary(a,c,-s,-c,s);}
Jet3 cb_exp(const Jet3& a){const auto e=checked(std::exp(a.c[0]));return cb_unary(a,e,e,e,e);}
Jet3 cb_expm1(const Jet3& a){const auto e=checked(std::exp(a.c[0]));return cb_unary(a,std::expm1(a.c[0]),e,e,e);}
Jet3 cb_log(const Jet3& a){if(a.c[0]<=0)throw std::domain_error("log requires positive input.");const auto inverse=1/a.c[0];return cb_unary(a,std::log(a.c[0]),inverse,-inverse*inverse,2*inverse*inverse*inverse);}
Jet3 cb_log1p(const Jet3& a){if(a.c[0]<=-1)throw std::domain_error("log1p requires input greater than-1.");const auto inverse=1/(1+a.c[0]);return cb_unary(a,std::log1p(a.c[0]),inverse,-inverse*inverse,2*inverse*inverse*inverse);}
Jet3 cb_log10(const Jet3& a){return cb_log(a)/Jet3(std::log(10.L));}
Jet3 cb_log2(const Jet3& a){return cb_log(a)/Jet3(std::log(2.L));}
Jet3 cb_sqrt(const Jet3& a){if(a.c[0]<0)throw std::domain_error("sqrt requires nonnegative input.");if(a.c[0]==0){if(a.dependent)throw std::domain_error("Third derivative of state-dependent sqrt at zero cannot be certified.");return Jet3(0);}const auto root=std::sqrt(a.c[0]);return cb_unary(a,root,.5L/root,-.25L/(a.c[0]*root),.375L/(a.c[0]*a.c[0]*root));}
Jet3 cb_sinh(const Jet3& a){const auto s=std::sinh(a.c[0]),c=std::cosh(a.c[0]);return cb_unary(a,s,c,s,c);}
Jet3 cb_cosh(const Jet3& a){const auto s=std::sinh(a.c[0]),c=std::cosh(a.c[0]);return cb_unary(a,c,s,c,s);}
Jet3 cb_tanh(const Jet3& a){const auto t=std::tanh(a.c[0]),e=std::exp(-2*std::abs(a.c[0])),d=4*e/((1+e)*(1+e));return cb_unary(a,t,d,-2*t*d,4*t*t*d-2*d*d);}
Jet3 cb_tan(const Jet3& a){const auto t=std::tan(a.c[0]),d=1+t*t;return cb_unary(a,t,d,2*t*d,2*d*(1+3*t*t));}
Jet3 cb_asin(const Jet3& a){const auto x=a.c[0];if(std::abs(x)>=1){if(a.dependent||std::abs(x)>1)throw std::domain_error("asin third derivative requires input strictly inside[-1,1].");return Jet3(std::asin(x));}const auto q=1-x*x,r=std::sqrt(q);return cb_unary(a,std::asin(x),1/r,x/(q*r),(1+2*x*x)/(q*q*r));}
Jet3 cb_acos(const Jet3& a){const auto x=a.c[0];if(std::abs(x)>=1){if(a.dependent||std::abs(x)>1)throw std::domain_error("acos third derivative requires input strictly inside[-1,1].");return Jet3(std::acos(x));}const auto q=1-x*x,r=std::sqrt(q);return cb_unary(a,std::acos(x),-1/r,-x/(q*r),-(1+2*x*x)/(q*q*r));}
Jet3 cb_atan(const Jet3& a){const auto x=a.c[0],q=1+x*x;return cb_unary(a,std::atan(x),1/q,-2*x/(q*q),(6*x*x-2)/(q*q*q));}
Jet3 cb_asinh(const Jet3& a){const auto x=a.c[0],q=1+x*x,r=std::sqrt(q);return cb_unary(a,std::asinh(x),1/r,-x/(q*r),(2*x*x-1)/(q*q*r));}
Jet3 cb_acosh(const Jet3& a){const auto x=a.c[0];if(x<=1){if(a.dependent||x<1)throw std::domain_error("acosh third derivative requires input greater than1.");return Jet3(0);}const auto q=x*x-1,r=std::sqrt(q);return cb_unary(a,std::acosh(x),1/r,-x/(q*r),(2*x*x+1)/(q*q*r));}
Jet3 cb_atanh(const Jet3& a){const auto x=a.c[0];if(std::abs(x)>=1)throw std::domain_error("atanh requires input strictly inside(-1,1).");const auto q=1-x*x;return cb_unary(a,std::atanh(x),1/q,2*x/(q*q),2*(1+3*x*x)/(q*q*q));}
Jet3 cb_abs(const Jet3& a){if(a.c[0]==0&&a.dependent)throw std::domain_error("abs is nondifferentiable at zero.");return a.c[0]<0?-a:a;}
Jet3 cb_floor(const Jet3& a){if(a.dependent&&a.c[0]==std::floor(a.c[0]))throw std::domain_error("floor is discontinuous at an integer.");return Jet3(std::floor(a.c[0]));}
Jet3 cb_ceil(const Jet3& a){if(a.dependent&&a.c[0]==std::floor(a.c[0]))throw std::domain_error("ceil is discontinuous at an integer.");return Jet3(std::ceil(a.c[0]));}
Jet3 cb_round(const Jet3& a){if(a.dependent&&std::abs(a.c[0]-std::trunc(a.c[0]))==.5L)throw std::domain_error("round is discontinuous at a half-integer.");return Jet3(std::round(a.c[0]));}
Jet3 cb_sign(const Jet3& a){if(a.dependent&&a.c[0]==0)throw std::domain_error("sign is discontinuous at zero.");return Jet3(a.c[0]>0?1:a.c[0]<0?-1:0);}
Jet3 cb_erf(const Jet3& a){const auto x=a.c[0],d=2/std::sqrt(std::acos(-1.L))*std::exp(-x*x);return cb_unary(a,std::erf(x),d,-2*x*d,(4*x*x-2)*d);}
Jet3 cb_erfc(const Jet3& a){const auto x=a.c[0],d=-2/std::sqrt(std::acos(-1.L))*std::exp(-x*x);return cb_unary(a,std::erfc(x),d,-2*x*d,(4*x*x-2)*d);}
Jet3 cb_hypot(const Jet3& a,const Jet3& b){const auto scale=std::max(std::abs(a.c[0]),std::abs(b.c[0]));if(scale==0){if(a.dependent||b.dependent)throw std::domain_error("hypot is nondifferentiable at the origin.");return Jet3(0);}const auto x=a/Jet3(scale),y=b/Jet3(scale);return cb_sqrt(x*x+y*y)*Jet3(scale);}
Jet3 cb_atan2(const Jet3& y,const Jet3& x){if(x.c[0]==0&&y.c[0]==0)throw std::domain_error("atan2 is undefined at the origin.");if(x.c[0]<0&&y.c[0]==0&&(x.dependent||y.dependent))throw std::domain_error("atan2 third derivative crosses its branch cut.");Jet3 result=std::abs(x.c[0])>=std::abs(y.c[0])?cb_atan(y/x):-cb_atan(x/y);result.c[0]=checked(std::atan2(y.c[0],x.c[0]));return result;}
Jet3 cb_ipow(Jet3 a,long long exponent){if(exponent==0)return Jet3(1);const bool negative=exponent<0;unsigned long long n=negative?static_cast<unsigned long long>(-exponent):static_cast<unsigned long long>(exponent);Jet3 result(1);while(n){if(n&1u)result=result*a;n>>=1u;if(n)a=a*a;}return negative?Jet3(1)/result:result;}
Jet3 cb_even(const Jet3& w,bool hyper,bool sinc){
    if(w.c[0]<0)throw std::domain_error("Invalid pure-part squared length.");
    long double f=0,d=0,d2=0,d3=0;const long double sign=hyper?1:-1;
    if(w.c[0]<1){long double coefficient=1;for(unsigned k=0;k<32;++k){if(k){const unsigned a=sinc?2*k:2*k-1,b=sinc?2*k+1:2*k;coefficient*=sign/(static_cast<long double>(a)*b);}f+=coefficient*std::pow(w.c[0],static_cast<int>(k));if(k)d+=k*coefficient*std::pow(w.c[0],static_cast<int>(k-1));if(k>1)d2+=k*(k-1)*coefficient*std::pow(w.c[0],static_cast<int>(k-2));if(k>2)d3+=k*(k-1)*(k-2)*coefficient*std::pow(w.c[0],static_cast<int>(k-3));}}
    else{const auto root=std::sqrt(w.c[0]),c=hyper?std::cosh(root):std::cos(root),s=(hyper?std::sinh(root):std::sin(root))/root,q=w.c[0];if(sinc){f=s;d=(c-s)/(2*q);d2=(sign*q*s-3*c+3*s)/(4*q*q);d3=((sign*q+15)*c-(6*sign*q+15)*s)/(8*q*q*q);}else{f=c;d=sign*s/2;d2=sign*(c-s)/(4*q);d3=sign*(sign*q*s-3*c+3*s)/(8*q*q);}}
    return cb_unary(w,f,d,d2,d3);
}
Pent<Jet3> cb_whole(const Pent<Jet3>& p,unsigned operation){Jet3 w(0);for(unsigned n=1;n<5;++n)w=w+p.c[n]*p.c[n];Pent<Jet3> result;Jet3 amplitude;
    if(operation==1){result.c[0]=cb_sin(p.c[0])*cb_even(w,true,false);amplitude=cb_cos(p.c[0])*cb_even(w,true,true);}
    else if(operation==2){result.c[0]=cb_cos(p.c[0])*cb_even(w,true,false);amplitude=-cb_sin(p.c[0])*cb_even(w,true,true);}
    else if(operation==3){const auto e=cb_exp(p.c[0]);result.c[0]=e*cb_even(w,false,false);amplitude=e*cb_even(w,false,true);}
    else if(operation==4){result.c[0]=cb_sinh(p.c[0])*cb_even(w,false,false);amplitude=cb_cosh(p.c[0])*cb_even(w,false,true);}
    else{result.c[0]=cb_cosh(p.c[0])*cb_even(w,false,false);amplitude=cb_sinh(p.c[0])*cb_even(w,false,true);}
    for(unsigned n=1;n<5;++n)result.c[n]=amplitude*p.c[n];
    return result;
}
template PENTA_NATIVE_API long double integerPower(long double a,unsigned exponent);
template PENTA_NATIVE_API Pent<long double> pent(long double a,long double b,long double c,long double d,long double e);
template PENTA_NATIVE_API Pent<long double> basis(unsigned i);
template PENTA_NATIVE_API Pent<long double> lift(long double x);
template PENTA_NATIVE_API Pent<long double> operator+(const Pent<long double>&a,const Pent<long double>&b);
template PENTA_NATIVE_API Pent<long double> operator-(const Pent<long double>&a,const Pent<long double>&b);
template PENTA_NATIVE_API Pent<long double> operator-(const Pent<long double>&a);
template PENTA_NATIVE_API Pent<long double> operator*(const Pent<long double>&a,const long double&b);
template PENTA_NATIVE_API Pent<long double> operator*(const long double&a,const Pent<long double>&b);
template PENTA_NATIVE_API Pent<long double> divide(const Pent<long double>&a,const long double&b);
template PENTA_NATIVE_API Pent<long double> product(const Pent<long double>&a,const Pent<long double>&b,double gamma);
template PENTA_NATIVE_API Pent<long double> power(Pent<long double>a,unsigned exponent,double gamma);
template PENTA_NATIVE_API Pent<long double> conjugate(Pent<long double>a);
template PENTA_NATIVE_API Pent<long double> pure(Pent<long double>a);
template PENTA_NATIVE_API long double norm2(const Pent<long double>&a);
template PENTA_NATIVE_API Pent<long double> commutator(const Pent<long double>&a,const Pent<long double>&b,double g);
template PENTA_NATIVE_API Pent<long double> associator(const Pent<long double>&a,const Pent<long double>&b,const Pent<long double>&c,double g);
template PENTA_NATIVE_API Pent<long double> cb_inverse(const Pent<long double>&a);
template PENTA_NATIVE_API Pent<long double> cb_pent_power(Pent<long double>a,long long n,double gamma);
template PENTA_NATIVE_API bool pent_equal(const Pent<long double>&a,const Pent<long double>&b);
template PENTA_NATIVE_API Jet2 integerPower(Jet2 a,unsigned exponent);
template PENTA_NATIVE_API Pent<Jet2> pent(Jet2 a,Jet2 b,Jet2 c,Jet2 d,Jet2 e);
template PENTA_NATIVE_API Pent<Jet2> basis(unsigned i);
template PENTA_NATIVE_API Pent<Jet2> lift(Jet2 x);
template PENTA_NATIVE_API Pent<Jet2> operator+(const Pent<Jet2>&a,const Pent<Jet2>&b);
template PENTA_NATIVE_API Pent<Jet2> operator-(const Pent<Jet2>&a,const Pent<Jet2>&b);
template PENTA_NATIVE_API Pent<Jet2> operator-(const Pent<Jet2>&a);
template PENTA_NATIVE_API Pent<Jet2> operator*(const Pent<Jet2>&a,const Jet2&b);
template PENTA_NATIVE_API Pent<Jet2> operator*(const Jet2&a,const Pent<Jet2>&b);
template PENTA_NATIVE_API Pent<Jet2> divide(const Pent<Jet2>&a,const Jet2&b);
template PENTA_NATIVE_API Pent<Jet2> product(const Pent<Jet2>&a,const Pent<Jet2>&b,double gamma);
template PENTA_NATIVE_API Pent<Jet2> power(Pent<Jet2>a,unsigned exponent,double gamma);
template PENTA_NATIVE_API Pent<Jet2> conjugate(Pent<Jet2>a);
template PENTA_NATIVE_API Pent<Jet2> pure(Pent<Jet2>a);
template PENTA_NATIVE_API Jet2 norm2(const Pent<Jet2>&a);
template PENTA_NATIVE_API Pent<Jet2> commutator(const Pent<Jet2>&a,const Pent<Jet2>&b,double g);
template PENTA_NATIVE_API Pent<Jet2> associator(const Pent<Jet2>&a,const Pent<Jet2>&b,const Pent<Jet2>&c,double g);
template PENTA_NATIVE_API Pent<Jet2> cb_inverse(const Pent<Jet2>&a);
template PENTA_NATIVE_API Pent<Jet2> cb_pent_power(Pent<Jet2>a,long long n,double gamma);
template PENTA_NATIVE_API bool pent_equal(const Pent<Jet2>&a,const Pent<Jet2>&b);
template PENTA_NATIVE_API Jet3 integerPower(Jet3 a,unsigned exponent);
template PENTA_NATIVE_API Pent<Jet3> pent(Jet3 a,Jet3 b,Jet3 c,Jet3 d,Jet3 e);
template PENTA_NATIVE_API Pent<Jet3> basis(unsigned i);
template PENTA_NATIVE_API Pent<Jet3> lift(Jet3 x);
template PENTA_NATIVE_API Pent<Jet3> operator+(const Pent<Jet3>&a,const Pent<Jet3>&b);
template PENTA_NATIVE_API Pent<Jet3> operator-(const Pent<Jet3>&a,const Pent<Jet3>&b);
template PENTA_NATIVE_API Pent<Jet3> operator-(const Pent<Jet3>&a);
template PENTA_NATIVE_API Pent<Jet3> operator*(const Pent<Jet3>&a,const Jet3&b);
template PENTA_NATIVE_API Pent<Jet3> operator*(const Jet3&a,const Pent<Jet3>&b);
template PENTA_NATIVE_API Pent<Jet3> divide(const Pent<Jet3>&a,const Jet3&b);
template PENTA_NATIVE_API Pent<Jet3> product(const Pent<Jet3>&a,const Pent<Jet3>&b,double gamma);
template PENTA_NATIVE_API Pent<Jet3> power(Pent<Jet3>a,unsigned exponent,double gamma);
template PENTA_NATIVE_API Pent<Jet3> conjugate(Pent<Jet3>a);
template PENTA_NATIVE_API Pent<Jet3> pure(Pent<Jet3>a);
template PENTA_NATIVE_API Jet3 norm2(const Pent<Jet3>&a);
template PENTA_NATIVE_API Pent<Jet3> commutator(const Pent<Jet3>&a,const Pent<Jet3>&b,double g);
template PENTA_NATIVE_API Pent<Jet3> associator(const Pent<Jet3>&a,const Pent<Jet3>&b,const Pent<Jet3>&c,double g);
template PENTA_NATIVE_API Pent<Jet3> cb_inverse(const Pent<Jet3>&a);
template PENTA_NATIVE_API Pent<Jet3> cb_pent_power(Pent<Jet3>a,long long n,double gamma);
template PENTA_NATIVE_API bool pent_equal(const Pent<Jet3>&a,const Pent<Jet3>&b);
}
