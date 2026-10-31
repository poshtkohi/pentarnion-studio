#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
int main(){int failed=0,checks=0;penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-pde-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";options.maxOperations=20000000;
auto run=[&](std::string source,bool good=true){++checks;const auto result=penta::runtime::execute("real pi=3.141592653589793;"+source,{},options);if(result.ok()!=good){++failed;std::cerr<<"FAIL: "<<source.substr(0,150)<<'\n';for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}return result;};
const std::string zero="native function real zero_reaction(real u,real t,real s){return 0.0;}";
for(const std::string method:{"fd","fem"}){
run(zero+"matrix x=linspace(0,1,33);matrix u=sin(pi*x);record f=pdesolve(zero_reaction,x,u,options(method=\""+method+"\",boundary=\"dirichlet\",diffusion=.1,t1=.1,time_step=.0001,sample_step=.05));matrix z=pdecoeff(f,0);assert(norm(z(end,:)-exp(-.1*pi*pi*.1)*u,2)<.001);assert(abs(z(end,1))<1e-14&&abs(z(end,end))<1e-14);assert(f.steps>=1000&&f.reaction_evaluations>1000);assert(f.status==\"completed\");");
run(zero+"matrix x=linspace(0,1,17);matrix u=ones(1,17)*3;record f=pdesolve(zero_reaction,x,u,options(method=\""+method+"\",boundary=\"neumann\",diffusion=.1,t1=.01,time_step=.0001));assert(norm(f.values(end,:)-u,2)<1e-12);assert(norm(f.mean-3,2)<1e-12);assert(norm(f.spatial_rms,2)<1e-12);");
run(zero+"matrix x=(0:31)/32;matrix u=cos(2*pi*x);record f=pdesolve(zero_reaction,x,u,options(method=\""+method+"\",length=1,diffusion=.1,t1=.1,time_step=.0001));assert(norm(f.values(end,:)-exp(-.1*4*pi*pi*.1)*u,2)<.005);");
}
run(zero+"matrix x=linspace(0,1,17);matrix u=sin(pi*x);record a=pdesolve(zero_reaction,x,u,options(boundary=\"dirichlet\",diffusion=.1,t1=.1,time_step=.0001));real e1=abs(a.values(end,9)-exp(-.01*pi*pi));matrix y=linspace(0,1,33);matrix v=sin(pi*y);record b=pdesolve(zero_reaction,y,v,options(boundary=\"dirichlet\",diffusion=.1,t1=.1,time_step=.0001));real e2=abs(b.values(end,17)-exp(-.01*pi*pi));assert(e1/e2>3.8&&e1/e2<4.2);");
run(zero+"matrix x=linspace(0,1,17);matrix u=sin(pi*x);record a=pdesolve(zero_reaction,x,u,options(method=\"fem\",boundary=\"dirichlet\",diffusion=.1,t1=.1,time_step=.0001));real e1=abs(a.values(end,9)-exp(-.01*pi*pi));matrix y=linspace(0,1,33);matrix v=sin(pi*y);record b=pdesolve(zero_reaction,y,v,options(method=\"fem\",boundary=\"dirichlet\",diffusion=.1,t1=.1,time_step=.0001));real e2=abs(b.values(end,17)-exp(-.01*pi*pi));assert(e1/e2>3.8&&e1/e2<4.2);");
run("native function real decay(real u,real t,real s,real rate){return -rate*u;}matrix x=(0:7)/8;record a=pdesolve(decay,x,ones(1,8),options(parameters=options(rate=1.0),length=1,diffusion=0,t1=.2,time_step=.001));record b=pdesolve(decay,x,ones(1,8),options(parameters=options(rate=2.0),length=1,diffusion=0,t1=.2,time_step=.001));assert(a.cache_key==b.cache_key&&b.cache_hit);assert(norm(a.values(end,:)-exp(-.2),2)<1e-10);assert(norm(b.values(end,:)-exp(-.4),2)<1e-10);");
run("native function pentarnion reaction(pentarnion p,real t,real s,pentarnion A,pentarnion B,real eta){return A*p+(1-eta)*(B*(p*p))+eta*((B*p)*p);}pentarnion A=-.5-2*i+.5*j-k-.5*ell;pentarnion B=1+2*j-.5*ell;pentarnion p0=(1-i-j+k-ell)/2;matrix x=(0:15)*1.25;matrix p=p0+.001*sin(2*pi*x/20)+.0007*cos(4*pi*x/20)*j+.0004*sin(6*pi*x/20+.3)*ell;record f=pdesolve(reaction,x,p,options(parameters=options(A=A,B=B,eta=0.0),length=20,diffusion=.1,t1=.02,time_step=.005,sample_step=.01));assert(abs(f.spatial_rms(1)-.0009082951062292429)<1e-13);assert(size(f.values,1)==3&&size(f.values,2)==16);matrix z=pdecoeff(f,4);assert(size(z,1)==3&&size(z,2)==16);assert(f.reaction_evaluations==256);");
for(const std::string method:{"fd","fem"})run(zero+"matrix x=linspace(0,1,17);matrix u=2+3*x;record f=pdesolve(zero_reaction,x,u,options(method=\""+method+"\",boundary=\"dirichlet\",left=2.0,right=5.0,diffusion=.1,t1=.1,time_step=.0001));assert(norm(f.values(end,:)-u,2)<1e-11);");
run(zero+"record f=pdesolve(zero_reaction,[-1e308,0,1e308],[1,1,1],options(boundary=\"neumann\"));",false);
run(zero+"record f=pdesolve(zero_reaction,[0,.5,1],[1,1,1],options(max_native_operations=1));",false);
run(zero+"record f=pdesolve(zero_reaction,[0,.1,.4],[1,1,1],options(method=\"fd\"));",false);
run(zero+"record f=pdesolve(zero_reaction,linspace(0,1,9),ones(1,9),options(boundary=\"dirichlet\",diffusion=1,time_step=.1));",false);
run(zero+"record f=pdesolve(zero_reaction,[0,.5,1],[1,1,1],options(boundary=\"periodic\",length=1));",false);
run(zero+"record f=pdesolve(zero_reaction,[0,.5,1],[1,1,1],options(max_reaction_evaluations=1));",false);
run(zero+"record f=pdesolve(zero_reaction,[0,.5,1],[1,1,1],options(boundary=\"robin\"));",false);
run(zero+"record f=pdesolve(zero_reaction,[0,.5,1],[1,1,1],options(typo=1));",false);
run(zero+"record f=pdesolve(zero_reaction,[0,.5,1],[1,1,1],options(boundary=\"neumann\",left=2.0));",false);
run(zero+"record f=pdesolve(zero_reaction,[0,.5,1],[1,1,1],options(boundary=\"dirichlet\",length=20));",false);
std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" PDE checks, "<<failed<<" failures\n";return failed?1:0;}
