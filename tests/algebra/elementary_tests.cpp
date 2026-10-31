#include "algebra/elementary.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>

using namespace penta::algebra;
namespace {
int checks=0;
void require(bool ok,const char* message) { ++checks;if(!ok)throw std::runtime_error(message); }
void near(const Pentarnion& actual,const Coefficients& expected,double tolerance=2e-12) {
    for(std::size_t n=0;n<5;++n) {
        require(std::abs(actual.coefficients()[n]-expected[n])<=tolerance*(1+std::abs(expected[n])),"Elementary coefficient mismatch");
    }
}
using Operation=Pentarnion(*)(const Pentarnion&);
const std::array<Operation,5> functions{sine,cosine,exponential,hyperbolicSine,hyperbolicCosine};
std::complex<double> reference(std::complex<double> z,std::size_t n) {
    switch(n) {
    case 0:return std::sin(z);case 1:return std::cos(z);case 2:return std::exp(z);
    case 3:return std::sinh(z);default:return std::cosh(z);
    }
}
void tests() {
    // Independent recurrence with the actual algebra product, not the slice
    // closed form: compare all five entire functions to their defining series.
    for(double gamma:{0.,0.37,1.}) {
        const Context context(gamma);
        for(const Coefficients c:{Coefficients{0,0,0,0,0},Coefficients{-0.7,0,0,0,0},
                                  Coefficients{0,0.2,-0.3,0.4,0.5},Coefficients{0.7,-0.2,0.3,0.4,-0.5}}) {
            const Pentarnion q(context,c);
            std::array<Coefficients,5> sums{};
            Pentarnion power=Pentarnion::scalar(context,1);
            double factorial=1;
            for(int n=0;n<65;++n) {
                if(n) {power=multiply(power,q);factorial*=n;}
                for(std::size_t component=0;component<5;++component) {
                    const double term=power.coefficients()[component]/factorial;
                    sums[2][component]+=term;
                    if(n%2) {sums[0][component]+=(n%4==1?term:-term);sums[3][component]+=term;}
                    else {sums[1][component]+=(n%4==0?term:-term);sums[4][component]+=term;}
                }
            }
            for(std::size_t n=0;n<5;++n) {
                const auto result=functions[n](q);
                near(result,sums[n]);
                require(result.context().gamma()==gamma,"Function lost algebra context");
            }
        }
        for(double a:{-2.,0.,0.4,3.}) for(double b:{-2.,-1e-9,0.,1.7}) {
            const Pentarnion q(context,{a,b,0,0,0});
            for(std::size_t n=0;n<5;++n) {
                const auto expected=reference({a,b},n);
                near(functions[n](q),{expected.real(),expected.imag(),0,0,0});
            }
        }
        // A tiny component must survive instead of being discarded by a
        // small-pure-part threshold or a prematurely underflowed ratio.
        const Pentarnion tiny(context,{0,1e-310,0,0,0});
        require(exponential(tiny).coefficients()[1]==1e-310,"Tiny exponential component lost");
        require(sine(tiny).coefficients()[1]==1e-310,"Tiny sine component lost");
        const double subnormal=std::numeric_limits<double>::denorm_min();
        const Pentarnion mixed(context,{0,10,subnormal,0,0});
        const double expectedTiny=static_cast<double>(std::sinh(10.0L)/10*static_cast<long double>(subnormal));
        require(sine(mixed).coefficients()[2]==expectedTiny&&expectedTiny!=0,
                "Mixed-scale sine prematurely underflowed a representable coefficient");
        const Pentarnion mixedExp(context,{10,10,subnormal,0,0});
        const double expectedExp=static_cast<double>(std::exp(10.0L)*std::sin(10.0L)/10*static_cast<long double>(subnormal));
        require(exponential(mixedExp).coefficients()[2]==expectedExp&&expectedExp!=0,
                "Mixed-scale exponential prematurely rounded a subnormal ratio");
        const auto extreme=exponential(Pentarnion(context,{709,1e308,1e-100,0,0}));
        require(std::isfinite(extreme.coefficients()[0]),"Scaled exponential overflowed prematurely");
        require(extreme.coefficients()[2]!=0,"Representable small direction disappeared");
        bool rejected=false;
        try {(void)exponential(Pentarnion::scalar(context,1000));}
        catch(const AlgebraError&) {rejected=true;}
        require(rejected,"Nonrepresentable exponential accepted");
        const auto q=Pentarnion(context,{0.3,0.1,-0.3,0.2,0.4});
        const auto s=sine(q),c=cosine(q);
        near(add(multiply(s,s),multiply(c,c)),{1,0,0,0,0});
        const auto sh=hyperbolicSine(q),ch=hyperbolicCosine(q);
        near(subtract(multiply(ch,ch),multiply(sh,sh)),{1,0,0,0,0});
    }
}
}
int main() {
    try {tests();std::cout<<"Elementary calculus passed: "<<checks<<" independent series/complex/context/limit checks.\n";return 0;}
    catch(const std::exception& e) {std::cerr<<"Elementary test failed at "<<checks<<": "<<e.what()<<'\n';return 1;}
}
