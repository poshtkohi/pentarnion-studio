#include "pentarnion/native_dual.hpp"
#include "original_reference.hpp"
#include <iostream>
#include <iomanip>
#include <string>
namespace B=penta_generated;namespace R=penta_reference;
int checks=0,failures=0;long double maximum=0;
void expect(bool value,const std::string& label){++checks;if(!value){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
void equal(long double a,long double b,const std::string& label){maximum=std::max(maximum,std::abs(a-b));expect(std::isfinite(a)&&std::isfinite(b)&&a==b,label);}
template<std::size_t N>void equal(const B::Dual<N>&a,const R::Dual<N>&b,const std::string& label){equal(a.v,b.v,label+" value");for(std::size_t k=0;k<N;++k)equal(a.d[k],b.d[k],label+" tangent");expect(a.stateDependent==b.stateDependent,label+" state dependency");}
template<class T>T seed(long double value,bool active=true){T r(value);for(std::size_t k=0;k<r.d.size();++k)r.d[k]=(k%2?-.17L:.13L)*(k+1);r.stateDependent=active;return r;}
template<class F>std::string error(F f){try{f();return "<success>";}catch(const std::exception&e){return e.what();}}
template<std::size_t N>void run(){
 using S=B::Dual<N>;using T=R::Dual<N>;const auto label="N="+std::to_string(N);
 expect(sizeof(S)==sizeof(T)&&offsetof(S,stateDependent)==offsetof(T,stateDependent),label+" ABI layout");equal(S(),T(),label+" default constructor");
 for(const long double value:{.125L,.7L,1.25L}){
  const auto a=seed<S>(value),b=seed<S>(value+.33L),x=seed<T>(value),y=seed<T>(value+.33L);
  equal(a+b,x+y,label+" plus");equal(a-b,x-y,label+" minus");equal(-a,-x,label+" negate");equal(a*b,x*y,label+" multiply");equal(a/b,x/y,label+" divide");equal(B::divide(a,b),R::divide(x,y),label+" named divide");equal(B::value(a),R::value(x),label+" value query");
#define M(F) equal(B::math_##F(a),R::math_##F(x),label+" math " #F)
  M(sin);M(cos);M(tan);M(exp);M(log);M(sqrt);M(sinh);M(cosh);M(tanh);
#undef M
  for(unsigned power:{0u,1u,2u,7u})equal(B::integerPower(a,power),R::integerPower(x,power),label+" integer power");
  const auto p=B::pent<S>(a,b,-a,S(.1L),S(-.2L)),q=B::pent<S>(b,a,S(.3L),S(.4L),S(.5L));
  const auto rp=R::pent<T>(x,y,-x,T(.1L),T(-.2L)),rq=R::pent<T>(y,x,T(.3L),T(.4L),T(.5L));
  const auto pentEqual=[&](const auto&first,const auto&second,const char*operation){for(unsigned k=0;k<5;++k)equal(first.c[k],second.c[k],label+operation);};
  pentEqual(p+q,rp+rq," Pent add");pentEqual(p-q,rp-rq," Pent subtract");pentEqual(-p,-rp," Pent negate");pentEqual(p*a,rp*x," Pent scale");pentEqual(a*p,x*rp," reverse scalar scale");pentEqual(B::divide(p,a),R::divide(rp,x)," Pent divide");
  for(double gamma:{0.,.3,1.}){pentEqual(B::product(p,q,gamma),R::product(rp,rq,gamma)," ordered product");pentEqual(B::commutator(p,q,gamma),R::commutator(rp,rq,gamma)," commutator");pentEqual(B::associator(p,q,p,gamma),R::associator(rp,rq,rp,gamma)," associator");for(unsigned power:{0u,1u,3u})pentEqual(B::power(p,power,gamma),R::power(rp,power,gamma)," Pent integer power");}
  pentEqual(B::conjugate(p),R::conjugate(rp)," conjugate");pentEqual(B::pure(p),R::pure(rp)," pure");equal(B::norm2(p),R::norm2(rp),label+" norm2");pentEqual(B::basis<S>(2),R::basis<T>(2)," basis");pentEqual(B::lift(a),R::lift(x)," lift");
 }
 const auto sameError=[&](auto f,auto g,const char*text){const auto a=error(f),b=error(g);expect(a!="<success>"&&a==b,label+text);};
 sameError([&]{B::math_sqrt(seed<S>(0));},[&]{R::math_sqrt(seed<T>(0));}," active zero sqrt");
 sameError([&]{B::math_sqrt(seed<S>(-1));},[&]{R::math_sqrt(seed<T>(-1));}," negative sqrt");
 sameError([&]{B::math_log(seed<S>(0));},[&]{R::math_log(seed<T>(0));}," nonpositive log");
 sameError([&]{(void)(seed<S>(1)/seed<S>(0));},[&]{(void)(seed<T>(1)/seed<T>(0));}," zero divisor");
 equal(B::math_sqrt(seed<S>(0,false)),R::math_sqrt(seed<T>(0,false)),label+" constant zero sqrt");
}
int main(){run<1>();run<5>();run<64>();std::cout<<checks<<" Dual bridge checks, "<<failures<<" failures; maximum absolute channel delta "<<std::setprecision(8)<<maximum<<'\n';return failures?1:0;}
