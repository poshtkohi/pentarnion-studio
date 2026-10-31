#include <pentarnion/native_dense.hpp>
#include <vector>
#include <algorithm>
namespace penta_generated {
namespace private_dense {
thread_local const BridgeDefinition* current=nullptr;
struct Scope {const BridgeDefinition* previous;explicit Scope(const BridgeDefinition* value):previous(current){current=value;}~Scope(){current=previous;}};
const BridgeDefinition& traits(){if(!current||current->dimension<1||current->dimension>64)throw std::domain_error("Invalid compiled algebra descriptor.");return *current;}
template<class S>struct Dynamic {std::vector<S> c;Dynamic():c(traits().dimension){}};
template<class S>Dynamic<S> universal_load(const S* values){Dynamic<S> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=values[k];return r;}
template<class S>Dynamic<S> universal_basis(unsigned index){Dynamic<S> r;r.c.at(index)=S(1);return r;}
template<class S>Dynamic<S> universal_lift(const S& scalar){
    if(!traits().has_unit)throw std::domain_error("Real scalar promotion requires an algebra unit.");
    Dynamic<S> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=S(traits().unit[k])*scalar;return r;
}
template<class S>Dynamic<S> operator+(const Dynamic<S>& a,const Dynamic<S>& b){Dynamic<S> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=a.c[k]+b.c[k];return r;}
template<class S>Dynamic<S> operator-(const Dynamic<S>& a,const Dynamic<S>& b){Dynamic<S> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=a.c[k]-b.c[k];return r;}
template<class S>Dynamic<S> operator-(const Dynamic<S>& a){Dynamic<S> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=-a.c[k];return r;}
template<class S>Dynamic<S> operator+(const Dynamic<S>& a){return a;}
template<class S>Dynamic<S> operator*(const Dynamic<S>& a,const S& b){Dynamic<S> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=a.c[k]*b;return r;}
template<class S>Dynamic<S> operator*(const S& a,const Dynamic<S>& b){return b*a;}
template<class S>Dynamic<S> divide(const Dynamic<S>& a,const S& b){Dynamic<S> r;for(unsigned k=0;k<r.c.size();++k)r.c[k]=divide(a.c[k],b);return r;}
template<class S>Dynamic<S> universal_product(const Dynamic<S>& a,const Dynamic<S>& b){
    Dynamic<S> r;for(const auto& term:traits().terms)r.c[term.output]=r.c[term.output]+(a.c[term.left]*b.c[term.right])*S(term.coefficient);return r;
}
template<class S>Dynamic<S> universal_conjugate(const Dynamic<S>& a){
    if(!traits().has_conjugation)throw std::domain_error("This algebra has no declared conjugation.");
    Dynamic<S> r;for(unsigned row=0;row<r.c.size();++row)for(unsigned column=0;column<r.c.size();++column){const auto coefficient=traits().conjugation[row*r.c.size()+column];if(coefficient!=0)r.c[row]=r.c[row]+a.c[column]*S(coefficient);}return r;
}
template<class S>S universal_coordinate_norm2(const Dynamic<S>& a){S r(0);for(const auto& coefficient:a.c)r=r+coefficient*coefficient;return r;}
template<class S>S universal_norm2(const Dynamic<S>& a){
    if(!traits().has_metric)throw std::domain_error("This algebra has no declared metric.");
    S r(0);for(unsigned row=0;row<a.c.size();++row)for(unsigned column=0;column<a.c.size();++column){const auto coefficient=traits().metric[row*a.c.size()+column];if(coefficient!=0)r=r+(a.c[row]*a.c[column])*S(coefficient);}return r;
}
template<class S>Dynamic<S> universal_pure(Dynamic<S> a){if(!traits().scalar_first)throw std::domain_error("No scalar coordinate functional is declared for this basis.");a.c[0]=S(0);return a;}
template<class S>bool universal_equal(const Dynamic<S>& a,const Dynamic<S>& b){bool equal=true;for(unsigned k=0;k<a.c.size();++k)equal=(a.c[k]==b.c[k])&&equal;return equal;}
template<class S>Dynamic<S> universal_inverse(const Dynamic<S>& a){
    if(!traits().has_unit)throw std::domain_error("An inverse requires an algebra unit.");
    // Every nonzero standard Cayley-Dickson element has this reciprocal. For
    // sedenions it does NOT imply invertible left/right multiplication maps.
    if(traits().canonical_reciprocal)return divide(universal_conjugate(a),universal_coordinate_norm2(a));
    else {
        if(!traits().associative_exact)throw std::domain_error("Native inverse derivatives require a certified associative algebra or a defined canonical reciprocal.");
        const unsigned n=traits().dimension;
        std::vector<S> matrix(n*n),rhs(n);std::vector<long double> inverse(n*n,0);for(unsigned i=0;i<n;++i){rhs[i]=S(traits().unit[i]);inverse[i*n+i]=1;}
        for(const auto& term:traits().terms)matrix[term.output*n+term.right]=matrix[term.output*n+term.right]+a.c[term.left]*S(term.coefficient);
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
        Dynamic<S> result;for(unsigned k=0;k<n;++k)result.c[k]=rhs[k];
        const auto left=universal_product(a,result),right=universal_product(result,a);long double residual=0,normal=1;
        for(unsigned k=0;k<n;++k){residual=std::max({residual,std::abs(value(left.c[k])-traits().unit[k]),std::abs(value(right.c[k])-traits().unit[k])});normal=std::max(normal,std::abs(value(a.c[k]))*std::abs(value(result.c[k])));}
        if(residual>1e-10L*normal){throw std::domain_error("Native inverse failed its two-sided residual check.");}return result;
    }
}
template<class S>Dynamic<S> universal_power(Dynamic<S> a,long long exponent){
    if(exponent<-64||exponent>64)throw std::domain_error("Native algebra powers are bounded to[-64,64].");
    if(exponent==0){return universal_lift<S>(S(1));}if(exponent<0){a=universal_inverse(a);exponent=-exponent;}
    // The explicit left fold matches the general algebra API without assuming
    // associativity or reassociating a user expression tree.
    auto result=a;for(long long k=1;k<exponent;++k)result=universal_product(result,a);return result;
}
template<class S>S universal_even(const S& w,bool hyper,bool sinc){
    if(std::abs(value(w))<1){S result(0),power(1);long double coefficient=1;for(unsigned k=0;k<32;++k){if(k){const unsigned a=sinc?2*k:2*k-1,b=sinc?2*k+1:2*k;coefficient*=(hyper?1.L:-1.L)/(static_cast<long double>(a)*b);power=power*w;}result=result+power*S(coefficient);}return result;}
    const bool negative=value(w)<0;const auto root=cb_sqrt(negative?-w:w);const bool actualHyper=negative?!hyper:hyper;
    return sinc?divide(actualHyper?cb_sinh(root):cb_sin(root),root):(actualHyper?cb_cosh(root):cb_cos(root));
}
template<class S>Dynamic<S> universal_whole(const Dynamic<S>& a,unsigned operation){
    if(traits().quadratic){
        S w(0);for(unsigned k=1;k<a.c.size();++k)w=w+a.c[k]*a.c[k]*S(traits().pure_square[k]);
        Dynamic<S> result;S amplitude;
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
        for(unsigned k=1;k<a.c.size();++k){result.c[k]=amplitude*a.c[k];}return result;
    } else {
        if(!traits().power_associative_exact||!traits().has_unit)throw std::domain_error("No native one-element calculus is certified for this algebra.");
        long double magnitude=0;for(const auto& coefficient:a.c)magnitude+=std::abs(value(coefficient));magnitude*=traits().product_bound;
        unsigned scaling=0;while(magnitude>.5L){magnitude*=.5L;if(++scaling>32)throw std::domain_error("General native elementary function exceeds its supported scaling range.");}
        const auto x=divide(a,S(std::ldexp(1.L,static_cast<int>(scaling))));const auto one=universal_lift<S>(S(1));
        if(operation==3){auto result=one,term=one;for(unsigned k=1;k<=64;++k){term=divide(universal_product(term,x),S(k));result=result+term;}for(unsigned k=0;k<scaling;++k)result=universal_product(result,result);return result;}
        const bool hyper=operation==4||operation==5;auto even=one,odd=x,term=one;
        for(unsigned k=1;k<=64;++k){term=divide(universal_product(term,x),S(k));const long double sign=hyper||((k/2)%2==0)?1.L:-1.L;if(k%2)odd= k==1?term:odd+term*S(sign);else even=even+term*S(sign);}
        for(unsigned k=0;k<scaling;++k){const auto nextOdd=universal_product(odd,even)+universal_product(even,odd);const auto nextEven=hyper?universal_product(even,even)+universal_product(odd,odd):universal_product(even,even)-universal_product(odd,odd);odd=nextOdd;even=nextEven;}
        return operation==1||operation==4?odd:even;
    }
}
template<class S>Dynamic<S> universal_complex_log(const Dynamic<S>& a){if(traits().dimension!=2)throw std::domain_error("A complex logarithm requires two coordinates.");Dynamic<S> result;result.c[0]=cb_log(cb_hypot(a.c[0],a.c[1]));result.c[1]=cb_atan2(a.c[1],a.c[0]);return result;}

} // private_dense
