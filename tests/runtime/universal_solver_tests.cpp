#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace penta;
namespace {
int checks=0;
void run(const std::string& source,const runtime::ExecutionOptions& options,bool success=true,bool staticFailure=false){
    ++checks;const auto result=runtime::execute(source,{},options);
    if(result.ok()==success&&(!staticFailure||!result.analysis.ok()))return;
    for(const auto& diagnostic:result.analysis.diagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<'\n';
    for(const auto& diagnostic:result.runtimeDiagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<" near "<<source.substr(diagnostic.span.offset,100)<<'\n';
    throw std::runtime_error("Unexpected generic solver result in fixture "+std::to_string(checks));
}
const std::string h="algebra H=builtin_algebra(\"quaternion\");";
}
int main(int argc,char** argv){
    runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-universal-solvers-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";
    options.numericModuleDirectory=argc>1?std::filesystem::path(argv[1]):std::filesystem::absolute(argv[0]).parent_path()/"modules";
    try{
        run(h+R"(
native function element<H> residual(element<H> p,element<H> a,element<H> b){return a*p-b;}
element target=1+H.i-2*H.j+0.25*H.k; element a=2+0.2*H.j;
record r=rootsolve(residual,0.0,options(parameters=options(a=a,b=a*target)));
assert(r.converged);assert(algebra_norm(r.x-target)<1e-8);assert(algebra_info(r.x).dimension==4);
record fd=rootsolve(residual,H.one,options(parameters=options(a=a,b=a*target),jacobian="finite_difference"));
assert(fd.converged);assert(fd.cache_hit);assert(algebra_norm(fd.x-target)<1e-7);
)",options);
        run(h+R"(
native function element<H> contraction(element<H> p,real rho,element<H> target){return rho*p+(1.0-rho)*target;}
element target=1+H.i-2*H.j+0.25*H.k;
record r=fixedpoint(contraction,0.0,options(parameters=options(rho=0.5,target=target)));
assert(r.converged);assert(algebra_norm(r.x-target)<1e-8);
record orbit=mapiterate(contraction,0.0,options(parameters=options(rho=0.5,target=target),iterations=25));
assert(algebra_norm(orbit.x-target)<1e-6);assert(size(orbit.states,2)==4);assert(size(orbit.jacobian,1)==4);
record scan=mapdiagram(contraction,H.one,"rho",[0.2,0.5,0.8],options(parameters=options(rho=0.5,target=target),iterations=5));
assert(size(scan.final_states,2)==4);assert(size(scan.scan_values,1)==3);
)",options);
        run(h+R"(
native function element<H> residual(element<H> p,element<H> target){return p-target;}
element target=1+H.i-2*H.j+0.25*H.k;
record r=lsqfit(residual,0.0,options(parameters=options(target=target),covariance=1));
assert(r.converged);assert(algebra_norm(r.x-target)<1e-7);assert(size(r.jacobian,1)==4);assert(size(r.jacobian,2)==4);
native function real energy(element<H> p,element<H> target){return norm2(p-target);}
record opt=minimize(energy,0.0,options(parameters=options(target=target)));
assert(opt.converged);assert(algebra_norm(opt.x-target)<1e-6);assert(opt.objective<1e-10);
matrix Hess=hessian(energy,H.one,options(parameters=options(target=target)));assert(norm(Hess-2*eye(4),"fro")<1e-11);
)",options);
        run(R"(
native function complex residual(complex z,complex target){return z*z-target;}
record r=rootsolve(residual,complex(0.7,0.2),options(parameters=options(target=complex(3,4))));
assert(r.converged);assert(abs(r.x-complex(2,1))<1e-7);
native function complex contraction(complex z){return 0.5*z+complex(1,0.5);}
record fixed=fixedpoint(contraction,complex(0,0));assert(fixed.converged);assert(abs(fixed.x-complex(2,1))<1e-8);
record orbit=mapiterate(contraction,complex(0,0),options(iterations=25));assert(abs(orbit.x-complex(2,1))<1e-6);assert(size(orbit.states,2)==2);
)",options);
        run(h+R"(
native function element<H> model(element<H> p,real t){return (1.0+t)*p;}
matrix data=[1,2,3,4;2,4,6,8;3,6,9,12];
record r=curvefit(model,0.0,[0,1,2],data);assert(r.converged);assert(algebra_norm(r.x-element(H,[1,2,3,4]))<1e-7);
assert(size(r.fitted,2)==4);
)",options);
        run(h+R"(
ode decay(t,element<H> p;real rate){p'=-rate*p;}
vector t=[0,0.2,0.5,1];matrix observed=[1,2,3,4;exp(-0.14),2*exp(-0.14),3*exp(-0.14),4*exp(-0.14);exp(-0.35),2*exp(-0.35),3*exp(-0.35),4*exp(-0.35);exp(-0.7),2*exp(-0.7),3*exp(-0.7),4*exp(-0.7)];
record r=odefit(decay,element(H,[1,2,3,4]),t,observed,options(parameters=options(rate=0.3),rtol=1e-10,atol=1e-12));
assert(r.converged);assert(abs(r.parameters.rate-0.7)<1e-6);assert(algebra_norm(r.initial-element(H,[1,2,3,4]))<1e-12);assert(size(r.jacobian,1)==16);
record equilibrium_result=equilibrium(decay,H.one,options(parameters=options(rate=0.7)));
assert(equilibrium_result.converged);assert(algebra_norm(equilibrium_result.x)<1e-10);
)",options);
        run(h+R"(
algebra Other=builtin_algebra("split_quaternion");
native function element<H> residual(element<H> p){return p-H.one;}
record r=rootsolve(residual,Other.one);
)",options,false,true);
        run(h+R"(
algebra Other=builtin_algebra("split_quaternion");
native function element<Other> wrong(element<H> p){return element(Other,[coeff(p,0),coeff(p,1),coeff(p,2),coeff(p,3)]);}
record r=mapiterate(wrong,H.one,options(iterations=2));
)",options,false);
        run(h+R"(
native function element<H> cubic(element<H> p){return p^3;}
record r=thirdderivative(cubic,H.one,H.one,H.one,H.one);
assert(r.output_coordinates==4);assert(r.output_widths==[4]);assert(norm(r.third-[6;0;0;0])<1e-12);
)",options);
        run(h+R"(
native function [element<H> q,complex z] mixed(element<H> p,complex c,element<H> target){return p-target,c-complex(2,1);}
record r=rootsolve(mixed,(H.one,complex(0,0)),options(parameters=options(target=2+3*H.j)));
assert(r.converged);[element<H> q,complex z]=r.x;assert(algebra_norm(q-(2+3*H.j))<1e-8);assert(abs(z-complex(2,1))<1e-8);
record fit=lsqfit(mixed,(H.one,complex(0,0)),options(parameters=options(target=2+3*H.j)));
assert(fit.converged);[element<H> fq,complex fz]=fit.x;assert(algebra_norm(fq-(2+3*H.j))<1e-7);assert(abs(fz-complex(2,1))<1e-7);
)",options);
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" generic native solver language fixtures passed.\n";
}
