#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>

// This header is embedded verbatim into every generated translation unit. No
// Studio, Eigen, Qt or SUNDIALS ABI crosses the compiled-model C interface.
namespace penta_generated {
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
} // namespace penta_generated
