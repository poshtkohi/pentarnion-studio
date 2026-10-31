#include "elementary.hpp"
#include <cmath>

namespace penta::algebra {
namespace {
enum class Function { Sin, Cos, Exp, Sinh, Cosh };

// Compute exp(exponent)*factor*(numerator/denominator) without requiring either
// exp(exponent) or the ratio to be individually representable in binary64.
double scaledExp(double exponent, double factor, double numerator=1, double denominator=1) {
    if (factor==0 || numerator==0) return 0;
    const double e=std::exp(exponent);
    const double ratio=numerator/denominator;
    const double f=factor*ratio;
    if (std::isfinite(e) && e>0 && std::abs(ratio)>=std::numeric_limits<double>::min()
        && std::abs(f)>=std::numeric_limits<double>::min()) return e*f;
    const double logarithm=exponent+std::log(std::abs(factor))+
                           std::log(std::abs(numerator))-std::log(denominator);
    const bool negative=std::signbit(factor)!=std::signbit(numerator);
    return std::copysign(std::exp(logarithm),negative?-1.0:1.0);
}

double scaledHyperbolic(double x, double factor, bool odd, double numerator=1, double denominator=1) {
    if (factor==0 || numerator==0) return 0;
    if (std::abs(x)<20) {
        const double amplitude=odd?std::sinh(x):std::cosh(x);
        const double ratio=numerator/denominator;
        if (amplitude!=0 && std::abs(ratio)<std::numeric_limits<double>::min())
            return scaledExp(std::log(std::abs(amplitude)),
                             std::copysign(factor,amplitude*factor),numerator,denominator);
        // Applying the bounded ratio last preserves very small vector entries.
        return (amplitude*factor)*ratio;
    }
    const double ax=std::abs(x);
    const double correction=odd?1-std::exp(-2*ax):1+std::exp(-2*ax);
    const double sign=odd&&x<0?-1.0:1.0;
    return scaledExp(ax-std::log(2.0),factor*sign*correction,numerator,denominator);
}

Pentarnion evaluate(const Pentarnion& value, Function function) {
    const auto& c=value.coefficients();
    const double a=c[0];
    const double r=std::hypot(std::hypot(c[1],c[2]),std::hypot(c[3],c[4]));
    if (!std::isfinite(r)) throw AlgebraError("R3001","Pure-part length is not representable for an elementary function.");
    Coefficients result{};
    if (r==0) {
        switch (function) {
        case Function::Sin: result[0]=std::sin(a);break;
        case Function::Cos: result[0]=std::cos(a);break;
        case Function::Exp: result[0]=std::exp(a);break;
        case Function::Sinh: result[0]=std::sinh(a);break;
        case Function::Cosh: result[0]=std::cosh(a);break;
        }
    } else {
        const double ca=std::cos(a), sa=std::sin(a), cr=std::cos(r), sr=std::sin(r);
        switch (function) {
        case Function::Exp:
            result[0]=scaledExp(a,cr);
            for (std::size_t n=1;n<5;++n) result[n]=scaledExp(a,sr,c[n],r);
            break;
        case Function::Sin:
            result[0]=scaledHyperbolic(r,sa,false);
            for (std::size_t n=1;n<5;++n) result[n]=scaledHyperbolic(r,ca,true,c[n],r);
            break;
        case Function::Cos:
            result[0]=scaledHyperbolic(r,ca,false);
            for (std::size_t n=1;n<5;++n) result[n]=scaledHyperbolic(r,-sa,true,c[n],r);
            break;
        case Function::Sinh:
            result[0]=scaledHyperbolic(a,cr,true);
            for (std::size_t n=1;n<5;++n) result[n]=scaledHyperbolic(a,sr,false,c[n],r);
            break;
        case Function::Cosh:
            result[0]=scaledHyperbolic(a,cr,false);
            for (std::size_t n=1;n<5;++n) result[n]=scaledHyperbolic(a,sr,true,c[n],r);
            break;
        }
    }
    for (const double component:result) {
        if (!std::isfinite(component)) throw AlgebraError("R3001","Elementary-function result is not finite or representable.");
    }
    return Pentarnion(value.context(),result);
}
}
Pentarnion sine(const Pentarnion& value) { return evaluate(value,Function::Sin); }
Pentarnion cosine(const Pentarnion& value) { return evaluate(value,Function::Cos); }
Pentarnion exponential(const Pentarnion& value) { return evaluate(value,Function::Exp); }
Pentarnion hyperbolicSine(const Pentarnion& value) { return evaluate(value,Function::Sinh); }
Pentarnion hyperbolicCosine(const Pentarnion& value) { return evaluate(value,Function::Cosh); }
}
