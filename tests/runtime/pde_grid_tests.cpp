#include "pde/pde_module.hpp"
#ifndef PENTA_PDE_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace {
int checks=0,failures=0;
void require(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
template<class F>void rejects(F&& function,const std::string& label){bool caught=false;try{function();}catch(const std::exception&){caught=true;}require(caught,label);}
constexpr double pi=std::numbers::pi;
struct Reaction {
    std::size_t components=1;double rate=0;bool force=false,fail=false;
    std::atomic_uint64_t calls{0};std::shared_ptr<std::atomic_bool> cancellation;
    static int one(void* raw,double t,double x,const double* state,double* out,char*,uint64_t){return two(raw,t,x,0,state,out,nullptr,0);}
    static int two(void* raw,double t,double x,double y,const double* state,double* out,char*,uint64_t){
        auto& self=*static_cast<Reaction*>(raw);const auto call=self.calls.fetch_add(1)+1;
        if(self.cancellation&&call>100)self.cancellation->store(true);
        if(self.fail&&x>.4)return 1;
        for(std::size_t j=0;j<self.components;++j)out[j]=-self.rate*state[j]+(self.force?(1+static_cast<double>(j))*(std::cos(t)+.2*2*pi*pi*std::sin(t))*std::sin(pi*x)*std::sin(pi*y):0);
        return 0;
    }
};
struct Grid {
    std::vector<double> x,y,u,boundary;
    penta_pde_grid_request_v1 request{};
    Grid(std::size_t n,unsigned bc,unsigned method,std::size_t components,Reaction& reaction):x(n),y(n),u(n*n*components),boundary(components,0){
        for(std::size_t i=0;i<n;++i)x[i]=y[i]=static_cast<double>(i)/static_cast<double>(bc==0?n:n-1);
        for(std::size_t iy=0;iy<n;++iy)for(std::size_t ix=0;ix<n;++ix)for(std::size_t j=0;j<components;++j)u[(iy*n+ix)*components+j]=(1+static_cast<double>(j))*(bc==0?std::cos(2*pi*x[ix])*std::cos(2*pi*y[iy]):std::sin(pi*x[ix])*std::sin(pi*y[iy]));
        reaction.components=components;
        request.struct_size=sizeof(request);request.abi_version=PENTA_PDE_GRID_ABI_V1;request.integrator=method;request.boundary=bc;request.components=static_cast<uint32_t>(components);request.nx=request.ny=n;request.x=x.data();request.y=y.data();request.initial=u.data();request.boundary_value=boundary.data();request.length_x=request.length_y=1;request.diffusion_x=request.diffusion_y=.1;request.t0=0;request.t1=.04;request.time_step=.0001;request.sample_step=.02;request.max_wall_seconds=30;request.max_steps=1000000;request.max_reaction_evaluations=200000000;request.reaction_context=&reaction;request.reaction=Reaction::two;
    }
};
penta::pde::Result solve(penta::pde::Module& module,Grid& grid,std::size_t threads=1){return module.solveGrid(grid.request,{},[](std::size_t){},threads);}
double error(const penta::pde::Result& result,const Grid& grid,double multiplier){double e=0;const auto count=grid.u.size(),offset=(result.report.samples-1)*count;for(std::size_t i=0;i<count;++i)e=std::max(e,std::abs(result.values[offset+i]-multiplier*grid.u[i]));return e;}
void nativeTests(){
    penta::pde::Module module;
    for(const unsigned bc:{0u,1u,2u})for(const unsigned method:{0u,1u})for(const std::size_t components:{1u,5u}){
        Reaction reaction;Grid grid(17,bc,method,components,reaction);
        if(bc==2)std::fill(grid.u.begin(),grid.u.end(),2.5);
        const auto serial=solve(module,grid,1),parallel=solve(module,grid,4);
        const std::string label="grid bc="+std::to_string(bc)+" method="+std::to_string(method)+" components="+std::to_string(components);
        require(serial.values==parallel.values&&serial.means==parallel.means&&serial.rms==parallel.rms,label+" deterministic serial/parallel");
        require(serial.report.steps==parallel.report.steps&&serial.report.reaction_evaluations==parallel.report.reaction_evaluations,label+" counts");
        require(parallel.statistics.parallelCalls>0&&parallel.statistics.requestedThreads==4&&parallel.statistics.effectiveThreads==4&&parallel.threadsGranted==4&&parallel.statistics.threadsUsed>=1&&parallel.statistics.threadsUsed<=4,label+" parallel dispatch, requested grant and bounded participants");
        require(serial.times==std::vector<double>({0,.02,.04}),label+" retained times");
        if(bc==2)require(error(serial,grid,1)<1e-11,label+" zero-flux constant and mass");
        else{
            const double h=grid.x[1],theta=(bc==0?2:1)*pi*h,lambda=8*.1*std::pow(std::sin(theta/2),2)/(h*h);
            const double amplification=method?std::pow(1+grid.request.time_step*lambda,-static_cast<double>(serial.report.steps)):std::exp(-lambda*grid.request.t1);
            require(error(serial,grid,amplification)<(method?1e-10:1e-12),label+" independent discrete eigenmode");
            if(bc==0)require(std::abs(serial.means.back())<1e-12,label+" periodic mean preserved");
        }
        require(method?(serial.linearSolves==serial.report.steps*components&&serial.factorizations>0&&serial.factorizations<serial.report.steps):serial.linearSolves==0,label+" honest implicit work report");
    }
    // Temporal convergence is compared to the exact semi-discrete eigenmode,
    // removing spatial error from the first-order IMEX check.
    double previous=0;
    for(const double step:{.02,.01,.005}){
        Reaction reaction;Grid grid(17,1,1,1,reaction);grid.request.time_step=step;grid.request.sample_step=grid.request.t1=.2;
        const double h=grid.x[1],lambda=.8*std::pow(std::sin(pi*h/2),2)/(h*h);
        const auto result=solve(module,grid);const double e=error(result,grid,std::exp(-lambda*.2));
        if(previous)require(previous/e>1.85&&previous/e<2.1,"IMEX first-order temporal convergence");
        previous=e;
    }
    // Spatial convergence uses a continuum heat mode with tiny RK4 time error.
    previous=0;
    for(const std::size_t n:{9u,17u,33u}){
        Reaction reaction;Grid grid(n,1,0,1,reaction);grid.request.time_step=.00001;grid.request.t1=grid.request.sample_step=.02;
        const auto result=solve(module,grid);const double e=error(result,grid,std::exp(-.2*pi*pi*.02));
        if(previous)require(previous/e>3.8&&previous/e<4.2,"2D second-order spatial convergence");
        previous=e;
    }
    // Independent forced manufactured problem: u=sin(t)sin(pi*x)sin(pi*y).
    {
        Reaction reaction;reaction.force=true;Grid grid(33,1,1,5,reaction);const auto mode=grid.u;std::fill(grid.u.begin(),grid.u.end(),0);grid.request.diffusion_x=grid.request.diffusion_y=.2;grid.request.time_step=.00002;grid.request.t1=grid.request.sample_step=.02;const auto result=solve(module,grid,4);
        double maximum=0;for(std::size_t i=0;i<mode.size();++i)maximum=std::max(maximum,std::abs(result.values[(result.report.samples-1)*mode.size()+i]-std::sin(.02)*mode[i]));
        std::cout<<"Manufactured max error: "<<maximum<<'\n';require(maximum<1e-5,"five-coefficient manufactured forcing (first-order dt=2e-5)");
    }
    // Diffusion-stiff step that RK4 must reject remains stable with IMEX.
    {
        Reaction reaction;Grid grid(33,1,1,1,reaction);grid.request.diffusion_x=grid.request.diffusion_y=1;grid.request.time_step=.01;grid.request.t1=grid.request.sample_step=.1;const auto result=solve(module,grid,4);require(error(result,grid,0)<1&&result.values[(result.report.samples-1)*grid.u.size()+16*33+16]>0,"implicit diffusion beyond explicit CFL is stable");grid.request.integrator=0;rejects([&]{solve(module,grid);},"explicit CFL remains enforced");
    }
    // 1D IMEX works with both FD and a consistent P1 FE mass matrix.
    for(const unsigned method:{0u,1u}){
        Reaction reaction;std::vector<double>x(33),u(33),boundary{0};for(std::size_t i=0;i<x.size();++i){x[i]=static_cast<double>(i)/32;u[i]=std::sin(pi*x[i]);}
        penta_pde_time_request_v1 timed{};auto& r=timed.base;r.struct_size=sizeof(timed);timed.integrator=1;r.nodes=x.size();r.components=1;r.method=method;r.boundary=1;r.x=x.data();r.initial=u.data();r.left=r.right=boundary.data();r.diffusion=.1;r.length=1;r.t1=.1;r.time_step=.01;r.sample_step=.1;r.max_wall_seconds=30;r.max_steps=10000;r.max_reaction_evaluations=1000000;r.reaction_context=&reaction;r.reaction=Reaction::one;
        const auto result=module.solve(r,{},[](std::size_t){},4);const double theta=pi/32,h=1.0/32,lambda=method?.1*6*(1-std::cos(theta))/(h*h*(2+std::cos(theta))):.1*4*std::pow(std::sin(theta/2),2)/(h*h);const double expected=std::pow(1+.01*lambda,-10);
        require(std::abs(result.values[(result.report.samples-1)*33+16]-expected)<1e-12,"1D FD/P1 mass IMEX discrete eigenmode");require(result.linearSolves==10,"1D IMEX linear solve count");
    }
    // Anisotropic coefficients and nonzero constant Dirichlet boundary data.
    {
        Reaction reaction;Grid grid(17,1,1,1,reaction);std::fill(grid.u.begin(),grid.u.end(),3);grid.boundary[0]=3;grid.request.diffusion_x=.03;grid.request.diffusion_y=1.7;const auto result=solve(module,grid,4);require(error(result,grid,1)<1e-11,"constant nonzero Dirichlet on all edges");
    }
    {
        Reaction reaction;Grid grid(17,0,1,1,reaction);grid.request.diffusion_x=.03;grid.request.diffusion_y=.17;const auto result=solve(module,grid,4);const double h=1.0/17,lambda=4*(.03+.17)*std::pow(std::sin(pi*h),2)/(h*h),amplification=std::pow(1+grid.request.time_step*lambda,-static_cast<double>(result.report.steps));require(error(result,grid,amplification)<1e-11,"anisotropic periodic diffusion");
    }
    {
        Reaction reaction;Grid grid(17,0,1,1,reaction);grid.request.nx=2;rejects([&]{solve(module,grid);},"invalid grid size");grid.request.nx=17;grid.x[4]+=.001;rejects([&]{solve(module,grid);},"nonuniform grid");grid.x[4]-=.001;grid.request.max_reaction_evaluations=1;rejects([&]{solve(module,grid);},"reaction quota");grid.request.max_reaction_evaluations=200000000;rejects([&]{module.solveGrid(grid.request,{},[](std::size_t){throw std::runtime_error("owner quota");},4);},"owner charge failure");reaction.fail=true;rejects([&]{solve(module,grid,4);},"worker failure joins safely");reaction.fail=false;require(solve(module,grid,4).report.samples==3,"pool reusable after error");
        auto cancellation=std::make_shared<std::atomic_bool>(false);reaction.cancellation=cancellation;reaction.calls=0;rejects([&]{module.solveGrid(grid.request,cancellation,[](std::size_t){},4);},"cooperative cancellation");reaction.cancellation.reset();require(solve(module,grid,4).report.samples==3,"pool reusable after cancellation");
    }
}
#ifndef PENTA_PDE_KERNEL_ONLY
void languageTests(){
    penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-grid-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";options.maxOperations=20000000;
    auto run=[&](const std::string& source,bool good=true){const auto result=penta::runtime::execute(source,{},options);require(result.ok()==good,"PDE grid language case");if(result.ok()!=good){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}};
    const std::string zero="native function real zero(real u,real t,real x,real y){return 0.0;}matrix x=linspace(0,1,9);matrix y=linspace(0,1,7);matrix u=ones(7,9)*3;";
    run(zero+"record f=pdesolve2(zero,x,y,u,options(integrator=\"imex_euler\",boundary=\"neumann\",diffusion=1.0,t1=.01,time_step=.01,sample_step=.01,threads=4));assert(f.dimension==2&&f.nx==9&&f.ny==7);matrix z=pdeslice(f,2);assert(size(z,1)==7&&size(z,2)==9);assert(norm(z-u,\"fro\")<1e-10);assert(f.integrator==\"imex_euler\"&&f.linear_solves==1);assert(f.threads_used<=4);");
    run("native function pentarnion zero(pentarnion u,real t,real x,real y){return u*0;}matrix x=linspace(0,1,9);matrix y=linspace(0,1,7);matrix u=ones(7,9)*(1+2*i+3*j+4*k+5*ell);record f=pdesolve2(zero,x,y,u,options(integrator=\"imex_euler\",boundary=\"neumann\",t1=.01,time_step=.01,sample_step=.01));matrix all=pdeslice(f,2);matrix z=pdeslice(f,2,4);assert(norm(z-5,\"fro\")<1e-10);assert(norm2(all(3,4)-(1+2*i+3*j+4*k+5*ell))<1e-20);");
    run(zero+"record f=pdesolve2(zero,x,y,u,options(boundary=\"neumann\",t1=.001,time_step=.001));matrix bad=pdeslice(f,0);",false);
    run(zero+"record f=pdesolve2(zero,x,y,u,options(boundary=\"neumann\",t1=.001,time_step=.001));matrix bad=pdeslice(f,1,4);",false);
    run(zero+"record f=pdesolve2(zero,x,y,ones(9,7));",false);
    run(zero+"record f=pdesolve2(zero,x,y,u,options(method=\"fem\"));",false);
    run(zero+"record f=pdesolve2(zero,x,y,u,options(boundary=\"neumann\",integrator=\"bdf\"));",false);
    run("native function real zero(real u,real t,real x){return 0.0;}matrix x=linspace(0,1,17);matrix u=ones(1,17);record f=pdesolve(zero,x,u,options(integrator=\"imex_euler\",method=\"fem\",boundary=\"neumann\",diffusion=1.0,t1=.01,time_step=.01,sample_step=.01));assert(f.integrator==\"imex_euler\"&&f.linear_solves==1&&f.dimension==1);assert(norm(f.values(end,:)-u,2)<1e-10);");
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){try{nativeTests();
#ifndef PENTA_PDE_KERNEL_ONLY
languageTests();
#endif
}catch(const std::exception& failure){++failures;std::cerr<<"UNEXPECTED: "<<failure.what()<<'\n';}std::cout<<checks<<" PDE grid/IMEX checks, "<<failures<<" failures\n";return failures?1:0;}
