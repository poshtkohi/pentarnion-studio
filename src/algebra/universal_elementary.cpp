#include "universal_internal.hpp"
#include <algorithm>
#include <cmath>

namespace penta::algebra::universal {
namespace {
enum class Operation { Sin,Cos,Exp,Sinh,Cosh };
bool proved(const Property& property){return property.value&&property.evidence==Evidence::ProvedExact;}
long double even(long double w,bool hyperbolic,bool sinc){
    if(std::abs(w)<1){long double sum=0,power=1,coefficient=1;for(unsigned k=0;k<32;++k){if(k){const unsigned a=sinc?2*k:2*k-1,b=sinc?2*k+1:2*k;coefficient*=(hyperbolic?1.L:-1.L)/(static_cast<long double>(a)*b);power*=w;}sum+=power*coefficient;}return sum;}
    const auto root=std::sqrt(std::abs(w));const bool hyper=w<0?!hyperbolic:hyperbolic;
    return sinc?(hyper?std::sinh(root):std::sin(root))/root:(hyper?std::cosh(root):std::cos(root));
}
Element evaluate(const Element& a,Operation operation){
    const auto& definition=*a.definition();const auto& kind=definition.builtinKind();
    const bool quadratic=kind=="real"||kind=="complex"||kind=="quaternion"||kind=="octonion"||kind=="sedenion"||kind=="dual"||kind=="split_complex"||kind=="split_quaternion";
    if(definition.unit().empty()||(!quadratic&&!proved(definition.properties().associative)&&!proved(definition.properties().powerAssociative)))throw Error("A4030","Whole-element functions require a unit and a proved one-element calculus.");
    if(quadratic){
        const auto& coefficients=a.coefficients();const long double scalar=coefficients[0];long double w=0;
        for(std::size_t k=1;k<coefficients.size();++k)w-=static_cast<long double>(coefficients[k])*coefficients[k]*definition.coefficient(k,k,0);
        long double constant=0,amplitude=0;
        if(operation==Operation::Exp&&w<-1){
            // Hyperbolic directions have real eigenvalues scalar +/- root.
            // Combining their exponentials avoids 0*infinity when each
            // separate exp(scalar)/cosh(root) factor is unrepresentable.
            const auto root=std::sqrt(-w);
            const auto plus=std::exp(scalar+root)/2,minus=std::exp(scalar-root)/2;
            constant=plus+minus;amplitude=(plus-minus)/root;
        }else switch(operation){
        case Operation::Sin:constant=std::sin(scalar)*even(w,true,false);amplitude=std::cos(scalar)*even(w,true,true);break;
        case Operation::Cos:constant=std::cos(scalar)*even(w,true,false);amplitude=-std::sin(scalar)*even(w,true,true);break;
        case Operation::Exp:constant=std::exp(scalar)*even(w,false,false);amplitude=std::exp(scalar)*even(w,false,true);break;
        case Operation::Sinh:constant=std::sinh(scalar)*even(w,false,false);amplitude=std::cosh(scalar)*even(w,false,true);break;
        case Operation::Cosh:constant=std::cosh(scalar)*even(w,false,false);amplitude=std::sinh(scalar)*even(w,false,true);break;
        }
        std::vector<double> result(coefficients.size());result[0]=detail::finite(constant);for(std::size_t k=1;k<result.size();++k)result[k]=detail::finite(amplitude*coefficients[k]);return Element(a.definition(),std::move(result));
    }
    // The coefficient L1 norm with this bilinear table bound is
    // submultiplicative. Scaling limits the Taylor argument to at most1/2.
    long double productBound=0,magnitude=0;std::vector<long double> sums(definition.dimension()*definition.dimension());
    for(const auto& term:definition.terms())sums[term.left*definition.dimension()+term.right]+=std::abs(term.coefficient);
    for(const auto sum:sums)productBound=std::max(productBound,sum);
    for(const auto value:a.coefficients())magnitude+=std::abs(value);
    magnitude*=productBound;unsigned scaling=0;while(magnitude>.5L){magnitude*=.5L;if(++scaling>32)throw Error("A4030","General elementary function exceeds its bounded scaling range.");}
    // Keep internal terms wide: a negligible Taylor coefficient may underflow
    // binary64 while a different coordinate of the final answer remains small
    // and representable. This also matches generated native jet precision.
    using Vector=std::vector<long double>;Vector x(a.coefficients().begin(),a.coefficients().end()),one(definition.unit().begin(),definition.unit().end());
    for(auto& value:x)value=std::ldexp(value,-static_cast<int>(scaling));
    auto product=[&](const Vector& left,const Vector& right){Vector out(definition.dimension());for(const auto& entry:definition.terms())out[entry.output]+=left[entry.left]*right[entry.right]*entry.coefficient;for(const auto value:out)if(!std::isfinite(value))throw Error("A4002","Elementary-function intermediate is not representable.");return out;};
    auto sum=[](Vector left,const Vector& right,long double sign=1){for(std::size_t n=0;n<left.size();++n)left[n]+=right[n]*sign;return left;};
    auto divide=[](Vector value,unsigned denominator){for(auto& coefficient:value)coefficient/=denominator;return value;};
    auto resultElement=[&](const Vector& values){std::vector<double> result;result.reserve(values.size());for(const auto value:values)result.push_back(detail::finite(value));return Element(a.definition(),std::move(result));};
    if(operation==Operation::Exp){auto result=one,term=one;for(unsigned k=1;k<=64;++k){term=divide(product(term,x),k);result=sum(std::move(result),term);}for(unsigned k=0;k<scaling;++k)result=product(result,result);return resultElement(result);}
    const bool hyper=operation==Operation::Sinh||operation==Operation::Cosh;auto evenPart=one,odd=x,term=one;
    for(unsigned k=1;k<=64;++k){term=divide(product(term,x),k);const long double sign=hyper||((k/2)%2==0)?1.L:-1.L;if(k%2)odd=k==1?term:sum(std::move(odd),term,sign);else evenPart=sum(std::move(evenPart),term,sign);}
    // These identities apply inside the certified associative algebra
    // generated by this one element, without reassociating user expressions.
    for(unsigned k=0;k<scaling;++k){auto nextOdd=sum(product(odd,evenPart),product(evenPart,odd));auto nextEven=sum(product(evenPart,evenPart),product(odd,odd),hyper?1.L:-1.L);odd=std::move(nextOdd);evenPart=std::move(nextEven);}
    return resultElement(operation==Operation::Sin||operation==Operation::Sinh?odd:evenPart);
}
}
Element sine(const Element& a){return evaluate(a,Operation::Sin);}
Element cosine(const Element& a){return evaluate(a,Operation::Cos);}
Element exponential(const Element& a){return evaluate(a,Operation::Exp);}
Element hyperbolicSine(const Element& a){return evaluate(a,Operation::Sinh);}
Element hyperbolicCosine(const Element& a){return evaluate(a,Operation::Cosh);}
}
