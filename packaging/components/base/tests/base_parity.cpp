#include "pentarnion/native_base.hpp"
#include "original_reference.hpp"
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

namespace B=penta_generated;
namespace R=penta_reference;
int checks=0,failures=0;
long double largestError=0;
void expect(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
void equal(long double a,long double b,const std::string& label){
    largestError=std::max(largestError,std::abs(a-b));
    expect(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=64*std::numeric_limits<long double>::epsilon()*std::max({1.L,std::abs(a),std::abs(b)}),label);
}
void equal(const B::Jet2& a,const R::Jet2& b,const std::string& label){
    equal(a.v,b.v,label+".v");equal(a.x,b.x,label+".x");equal(a.y,b.y,label+".y");equal(a.xy,b.xy,label+".xy");expect(a.dependent==b.dependent,label+".dependent");
}
void equal(const B::Jet3& a,const R::Jet3& b,const std::string& label){
    for(unsigned i=0;i<8;++i)equal(a.c[i],b.c[i],label+".c"+std::to_string(i));expect(a.dependent==b.dependent,label+".dependent");
}
template<class S>S seed(long double value){
    S result(value);
    if constexpr(requires{result.xy;}){result.x=.31L;result.y=-.27L;result.xy=.13L;}
    else {result.c[1]=.31L;result.c[2]=-.27L;result.c[3]=.13L;result.c[4]=.17L;result.c[5]=-.19L;result.c[6]=.23L;result.c[7]=-.29L;}
    result.dependent=true;return result;
}
template<class F>std::string error(F operation){try{operation();return "<success>";}catch(const std::exception& e){return e.what();}}
template<class F,class G>void sameError(F a,G b,const std::string& label){const auto first=error(a),second=error(b);expect(first!="<success>"&&first==second,label);}

#define UNARY(F,V) equal(B::F(seed<BS>(V)),R::F(seed<RS>(V)),label+" " #F)
#define BINARY(F,A,C) equal(B::F(seed<BS>(A),seed<BS>(C)),R::F(seed<RS>(A),seed<RS>(C)),label+" " #F)
template<class BS,class RS>void scalarParity(const std::string& label){
    for(const long double input:{.125L,.31L,.7L}){
        UNARY(cb_sin,input);UNARY(cb_cos,input);UNARY(cb_exp,input);UNARY(cb_expm1,input);
        UNARY(cb_log,input);UNARY(cb_log1p,input);UNARY(cb_log10,input);UNARY(cb_log2,input);UNARY(cb_sqrt,input);
        UNARY(cb_sinh,input);UNARY(cb_cosh,input);UNARY(cb_tanh,input);UNARY(cb_tan,input);
        UNARY(cb_asin,input);UNARY(cb_acos,input);UNARY(cb_atan,input);UNARY(cb_asinh,input);UNARY(cb_acosh,input+1);UNARY(cb_atanh,input);
        UNARY(cb_abs,-input);UNARY(cb_floor,input);UNARY(cb_ceil,input);UNARY(cb_round,input);UNARY(cb_sign,-input);UNARY(cb_erf,input);UNARY(cb_erfc,input);
        BINARY(cb_hypot,input,input+.4L);BINARY(cb_atan2,input,-input-.4L);
        auto a=seed<BS>(input),b=seed<BS>(input+.4L);auto x=seed<RS>(input),y=seed<RS>(input+.4L);
        equal(a+b,x+y,label+" add");equal(a-b,x-y,label+" subtract");equal(a*b,x*y,label+" multiply");equal(a/b,x/y,label+" divide");equal(-a,-x,label+" negate");equal(+a,+x,label+" positive");
        expect((a<b)==(x<y),label+" less");expect((a<=b)==(x<=y),label+" less_equal");expect((a>b)==(x>y),label+" greater");expect((a>=b)==(x>=y),label+" greater_equal");expect((a==b)==(x==y),label+" equal");expect((a!=b)==(x!=y),label+" not_equal");
        for(const long long exponent:{-3,0,1,7})equal(B::cb_ipow(a,exponent),R::cb_ipow(x,exponent),label+" integer_power");
    }
    for(const long double w:{0.L,1e-12L,.009L,.4L,2.L,40.L})for(const bool hyper:{false,true})for(const bool sinc:{false,true})
        equal(B::cb_even(seed<BS>(w),hyper,sinc),R::cb_even(seed<RS>(w),hyper,sinc),label+" analytic_even");
    sameError([&]{B::cb_sqrt(seed<BS>(-1));},[&]{R::cb_sqrt(seed<RS>(-1));},label+" negative_sqrt");
    sameError([&]{B::cb_log(seed<BS>(0));},[&]{R::cb_log(seed<RS>(0));},label+" zero_log");
    sameError([&]{B::cb_abs(seed<BS>(0));},[&]{R::cb_abs(seed<RS>(0));},label+" abs_boundary");
    sameError([&]{B::cb_asin(seed<BS>(1));},[&]{R::cb_asin(seed<RS>(1));},label+" asin_boundary");
    sameError([&]{B::cb_acosh(seed<BS>(1));},[&]{R::cb_acosh(seed<RS>(1));},label+" acosh_boundary");
    sameError([&]{B::cb_atan2(seed<BS>(0),seed<BS>(-1));},[&]{R::cb_atan2(seed<RS>(0),seed<RS>(-1));},label+" atan2_branch");
    sameError([&]{(void)(seed<BS>(1)/seed<BS>(0));},[&]{(void)(seed<RS>(1)/seed<RS>(0));},label+" zero_divisor");
    sameError([&]{(void)static_cast<bool>(seed<BS>(0));},[&]{(void)static_cast<bool>(seed<RS>(0));},label+" active_branch");
    sameError([&]{(void)(seed<BS>(1)==seed<BS>(1));},[&]{(void)(seed<RS>(1)==seed<RS>(1));},label+" active_comparison");
}
#undef UNARY
#undef BINARY

template<class BS,class RS>void pentParity(const std::string& label){
    for(const double gamma:{0.,.3,1.}){
        auto a=B::pent<BS>(seed<BS>(.2L),seed<BS>(.3L),seed<BS>(-.1L),seed<BS>(.15L),seed<BS>(-.25L));
        auto b=B::pent<BS>(seed<BS>(-.17L),seed<BS>(.21L),seed<BS>(.11L),seed<BS>(-.27L),seed<BS>(.09L));
        auto x=R::pent<RS>(seed<RS>(.2L),seed<RS>(.3L),seed<RS>(-.1L),seed<RS>(.15L),seed<RS>(-.25L));
        auto y=R::pent<RS>(seed<RS>(-.17L),seed<RS>(.21L),seed<RS>(.11L),seed<RS>(-.27L),seed<RS>(.09L));
        const auto compare=[&](const auto& first,const auto& second,const char* operation){for(unsigned k=0;k<5;++k)equal(first.c[k],second.c[k],label+" "+operation);};
        compare(a+b,x+y,"add");compare(a-b,x-y,"subtract");compare(-a,-x,"negate");
        compare(a*seed<BS>(.7L),x*seed<RS>(.7L),"scale");compare(B::divide(a,seed<BS>(.7L)),R::divide(x,seed<RS>(.7L)),"divide");
        compare(B::product(a,b,gamma),R::product(x,y,gamma),"ordered_product");
        compare(B::conjugate(a),R::conjugate(x),"conjugate");compare(B::pure(a),R::pure(x),"pure");equal(B::norm2(a),R::norm2(x),label+" norm2");
        compare(B::commutator(a,b,gamma),R::commutator(x,y,gamma),"commutator");
        compare(B::associator(a,b,a,gamma),R::associator(x,y,x,gamma),"associator");
        compare(B::cb_inverse(a),R::cb_inverse(x),"reciprocal");
        for(const long long exponent:{-2,0,1,3})compare(B::cb_pent_power(a,exponent,gamma),R::cb_pent_power(x,exponent,gamma),"power");
        for(unsigned operation=1;operation<=5;++operation)compare(B::cb_whole(a,operation),R::cb_whole(x,operation),"whole_elementary");
        for(auto& coefficient:a.c)coefficient=BS(0);for(auto& coefficient:x.c)coefficient=RS(0);
        a.c[0]=seed<BS>(.2L);x.c[0]=seed<RS>(.2L);
        for(unsigned operation=1;operation<=5;++operation)compare(B::cb_whole(a,operation),R::cb_whole(x,operation),"real_subcase");
    }
}

int main(){
    expect(penta_native_bridge_abi_v1()==1,"ABI query");
    expect(penta_native_bridge_long_double_size_v1()==sizeof(long double),"long double ABI");
    expect(sizeof(B::Jet2)==sizeof(R::Jet2)&&offsetof(B::Jet2,dependent)==offsetof(R::Jet2,dependent),"Jet2 layout");
    expect(sizeof(B::Jet3)==sizeof(R::Jet3)&&offsetof(B::Jet3,dependent)==offsetof(R::Jet3,dependent),"Jet3 layout");
    scalarParity<B::Jet2,R::Jet2>("AD2");scalarParity<B::Jet3,R::Jet3>("AD3");
    pentParity<B::Jet2,R::Jet2>("Pent AD2");pentParity<B::Jet3,R::Jet3>("Pent AD3");
    const auto a=B::pent<long double>(1,.2L,-.3L,.4L,.1L),b=B::basis<long double>(2);
    const auto x=R::pent<long double>(1,.2L,-.3L,.4L,.1L),y=R::basis<long double>(2);
    const auto p=B::product(a,b,.3);
    const auto q=R::product(x,y,.3);
    for(unsigned k=0;k<5;++k)equal(p.c[k],q.c[k],"plain long-double Pent product");
    equal(B::integerPower(.3L,7),R::integerPower(.3L,7),"plain integerPower");
    sameError([]{(void)B::binary64(std::numeric_limits<long double>::max());},[]{(void)R::binary64(std::numeric_limits<long double>::max());},"binary64 overflow");
    std::cout<<checks<<" base bridge checks, "<<failures<<" failures; maximum absolute channel delta "<<std::setprecision(8)<<largestError<<'\n';
    return failures?1:0;
}
