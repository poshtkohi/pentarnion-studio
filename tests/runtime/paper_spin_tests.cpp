#include "algebra/pentarnion.hpp"
#include "ode/native_ode.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

int main(){
    using namespace penta;using namespace algebra;
    int checks=0,failures=0;
    auto expect=[&](bool okay,const char* message){++checks;if(!okay){++failures;std::cerr<<message<<'\n';}};
    try{
        for(const double gamma:{0.0,.125,.5,1.0}){
            const Context context(gamma);
            const Pentarnion i(context,{0,1,0,0,0}),j(context,{0,0,1,0,0}),k(context,{0,0,0,1,0}),ell(context,{0,0,0,0,1});
            const auto a=scale(add(add(i,j),k),1/std::sqrt(3.));
            const auto b=scale(subtract(i,j),1/std::sqrt(2.));
            const auto c=scale(subtract(add(i,j),scale(k,2)),1/std::sqrt(6.));
            const auto h=scale(add(scale(a,std::sqrt(3.)),scale(ell,gamma)),1/std::sqrt(3+gamma*gamma));
            const auto normal=scale(add(scale(a,-gamma),scale(ell,std::sqrt(3.))),1/std::sqrt(3+gamma*gamma));
            const auto squared=[](const Pentarnion& p){double sum=0;for(const auto v:p.coefficients())sum+=v*v;return sum;};
            const double A=3*(std::pow(gamma,4)+2*gamma*gamma+3)/((1+gamma*gamma)*(3+gamma*gamma));
            const double B=(4*gamma*gamma+3)/(1+gamma*gamma);
            expect(std::abs(squared(h)-1)<1e-14&&std::abs(squared(normal)-1)<1e-14,"Paper adapted h/normal are unit for every tested gamma.");
            expect(std::abs(squared(add(h,normal))-2)<1e-14,"Paper h/normal are orthogonal.");
            for(unsigned sample=1;sample<=12;++sample){
                const double yh=std::sin(sample*.31),yb=std::cos(sample*.71),yc=std::sin(sample*.29);
                const auto x=add(add(scale(h,yh),scale(b,yb)),scale(c,yc));
                double energy=0;for(const auto& q:{i,j,k})energy+=.25*(squared(multiply(q,x))+squared(multiply(x,q)));
                expect(std::abs(energy-.5*(A*yh*yh+B*(yb*yb+yc*yc)))<2e-13,"Six ordered table products agree with the paper's independent energy formula.");
                expect(std::abs(3*x.coefficients()[4]-gamma*(x.coefficients()[1]+x.coefficients()[2]+x.coefficients()[3]))<1e-14,"The full coefficient embedding lies in the paper's H_gamma.");
                const double g1=1/std::sqrt(3+gamma*gamma),g2=1/std::sqrt(2.),g3=1/std::sqrt(6.);
                const double fh=-g3*yb+g2*yc,fb=(A-B)*yh*yc-g1*yc+g3*yh,fc=(B-A)*yh*yb-g2*yh+g1*yb;
                expect(std::abs(yh*fh+yb*fb+yc*fc)<1e-14,"Paper spin vector field is tangent to length spheres.");
                expect(std::abs((A*yh-g1)*fh+(B*yb-g2)*fb+(B*yc-g3)*fc)<1e-14,"Paper spin energy gradient is orthogonal to its real Lie-Poisson vector field.");
            }
        }
    }catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    std::cout<<checks<<" independent paper spin/geometry checks, "<<failures<<" failures\n";
    return failures?1:0;
}
