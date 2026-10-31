#pragma once
// Independent mathematical references for bounded third-order callback tests.
// These helpers deliberately contain no production Jet3 or generated-code API.
#include <array>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <string_view>

namespace penta::test::third_reference {
using Vector2=std::array<long double,2>;
using Coefficients=std::array<long double,5>;

// Ordinary real intrinsic third derivatives on the smooth interior domain.
inline long double intrinsic(std::string_view name,long double x){
    const auto square=x*x;
    if(name=="sin")return -std::cos(x);
    if(name=="cos")return std::sin(x);
    if(name=="exp"||name=="expm1")return std::exp(x);
    if(name=="sinh")return std::cosh(x);
    if(name=="cosh")return std::sinh(x);
    if(name=="tan"){const auto t=std::tan(x);return 2*(1+t*t)*(1+3*t*t);}
    if(name=="tanh"){const auto t=std::tanh(x);return 2*(1-t*t)*(3*t*t-1);}
    if(name=="asin"||name=="acos"){
        if(std::abs(x)>=1)throw std::domain_error("Reference inverse-trigonometric derivative is singular/outside domain.");
        const auto q=1-square;return (name=="asin"?1.L:-1.L)*(1+2*square)/(q*q*std::sqrt(q));
    }
    if(name=="atan"){const auto q=1+square;return (6*square-2)/(q*q*q);}
    if(name=="asinh"){const auto q=1+square;return (2*square-1)/(q*q*std::sqrt(q));}
    if(name=="acosh"){
        if(x<=1)throw std::domain_error("Reference acosh derivative requires x>1.");
        const auto q=square-1;return (2*square+1)/(q*q*std::sqrt(q));
    }
    if(name=="atanh"){
        if(std::abs(x)>=1)throw std::domain_error("Reference atanh derivative requires |x|<1.");
        const auto q=1-square;return (2+6*square)/(q*q*q);
    }
    if(name=="log"||name=="log10"||name=="log2"){
        if(x<=0)throw std::domain_error("Reference logarithmic derivative requires x>0.");
        const auto base=name=="log10"?std::log(10.L):name=="log2"?std::log(2.L):1.L;
        return 2/(x*x*x*base);
    }
    if(name=="log1p"){
        if(x<=-1)throw std::domain_error("Reference log1p derivative requires x>-1.");
        const auto q=1+x;return 2/(q*q*q);
    }
    if(name=="sqrt"){
        if(x<=0)throw std::domain_error("Reference sqrt derivative requires x>0.");
        return 3/(8*x*x*std::sqrt(x));
    }
    if(name=="erf"||name=="erfc")return (name=="erf"?1.L:-1.L)*2/std::sqrt(std::acos(-1.L))*(4*square-2)*std::exp(-square);
    if(name=="abs"||name=="floor"||name=="ceil"||name=="round"||name=="sign")return 0;
    throw std::invalid_argument("Unknown real intrinsic reference.");
}

// Independent 0F1 representation, rather than the production closed radial
// derivatives. C_s(w)=0F1(;1/2;s*w/4), G_s(w)=0F1(;3/2;s*w/4).
// Differentiation shifts the denominator parameter: (d/dw)^j 0F1(;b;s*w/4)
// = s^j/(4^j*(b)_j) * 0F1(;b+j;s*w/4).
// The bounded reference range is intended for origin/switch-point tests.
inline long double radial(long double w,bool hyperbolic,bool sinc,unsigned derivative){
    if(w<0||w>64||derivative>3)throw std::invalid_argument("Radial reference supports 0<=w<=64 and derivatives0..3.");
    const long double sign=hyperbolic?1.L:-1.L;
    long double b=sinc?1.5L:.5L,prefactor=1;
    for(unsigned j=0;j<derivative;++j)prefactor*=sign/(4*(b+j));
    b+=derivative;
    long double term=1,total=1;
    for(unsigned n=1;n<=256;++n){term*=sign*w/(4*n*(b+n-1));total+=term;}
    return prefactor*total;
}

inline long double xyz(const std::array<long double,3>& u,
                       const std::array<long double,3>& v,
                       const std::array<long double,3>& w){
    // D^3(x*y*z)[u,v,w], including every allocation of the three directions.
    return u[0]*(v[1]*w[2]+v[2]*w[1])
          +u[1]*(v[0]*w[2]+v[2]*w[0])
          +u[2]*(v[0]*w[1]+v[1]*w[0]);
}
inline long double xCubedYSquared(long double x,long double y,const Vector2& u,const Vector2& v,const Vector2& w){
    return 6*y*y*u[0]*v[0]*w[0]
        +12*x*y*(u[0]*v[0]*w[1]+u[0]*v[1]*w[0]+u[1]*v[0]*w[0])
        +6*x*x*(u[0]*v[1]*w[1]+u[1]*v[0]*w[1]+u[1]*v[1]*w[0]);
}
inline long double dot(const Vector2& a,const Vector2& b){return a[0]*b[0]+a[1]*b[1];}
inline long double hypotThird(const Vector2& point,const Vector2& u,const Vector2& v,const Vector2& w){
    const auto r=std::hypot(point[0],point[1]);if(r==0)throw std::domain_error("Reference norm derivative is undefined at the origin.");
    const auto xu=dot(point,u),xv=dot(point,v),xw=dot(point,w);
    return 3*xu*xv*xw/(r*r*r*r*r)-(dot(u,v)*xw+dot(u,w)*xv+dot(v,w)*xu)/(r*r*r);
}
inline long double atan2ThirdXY(const Vector2& point,const Vector2& u,const Vector2& v,const Vector2& w){
    // theta=Im(log(x+i*y)); D^3 theta=Im(2*u*v*w/z^3).
    if(point[0]==0&&point[1]==0)throw std::domain_error("Reference angle derivative is undefined at the origin.");
    const std::complex<long double> z(point[0],point[1]);
    return std::imag(2.L*(std::complex<long double>(u[0],u[1])/z)
        *(std::complex<long double>(v[0],v[1])/z)
        *(std::complex<long double>(w[0],w[1])/z));
}

inline long double pureDot(const Coefficients& a,const Coefficients& b){long double result=0;for(unsigned n=1;n<5;++n)result+=a[n]*b[n];return result;}
inline Coefficients wholeAtReal(std::string_view name,long double a,const Coefficients& u,const Coefficients& v,const Coefficients& w){
    // At p=a*1, derive directly from the one-element real/pure Taylor series.
    // C(w)=1+s*w/2+..., G(w)=1+s*w/6+..., and F'''(a)=-s*F'(a).
    long double slope=0,sign=0;
    if(name=="sin"){slope=std::cos(a);sign=1;}
    else if(name=="cos"){slope=-std::sin(a);sign=1;}
    else if(name=="exp"){slope=std::exp(a);sign=-1;}
    else if(name=="sinh"){slope=std::cosh(a);sign=-1;}
    else if(name=="cosh"){slope=std::sinh(a);sign=-1;}
    else throw std::invalid_argument("Whole-Pentarnion reference supports sin/cos/exp/sinh/cosh.");
    const auto uv=pureDot(u,v),uw=pureDot(u,w),vw=pureDot(v,w);
    Coefficients result{};
    result[0]=sign*slope*(-u[0]*v[0]*w[0]+u[0]*vw+v[0]*uw+w[0]*uv);
    for(unsigned n=1;n<5;++n)result[n]=sign*slope*
        (-(u[0]*v[0]*w[n]+u[0]*w[0]*v[n]+v[0]*w[0]*u[n])+(uv*w[n]+uw*v[n]+vw*u[n])/3);
    return result;
}

inline Coefficients orderedBasisThird(long double gamma,bool rightAssociated){
    // For F(a,b,c)=(a*b)*c or a*(b*c), choose the three input-block seeds
    // i,j,ell. The mixed derivative is the ordered product itself. This
    // published-table reference does not call production multiplication.
    const auto kappa=1/std::sqrt(1+gamma*gamma),sigma=gamma*kappa,h=1/std::sqrt(2.L);
    return rightAssociated?Coefficients{h,0,h*kappa,0,h*sigma}
                          :Coefficients{-sigma,-h*kappa,-h*kappa,0,0};
}
}
