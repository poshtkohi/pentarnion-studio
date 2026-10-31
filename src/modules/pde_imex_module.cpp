#include "pde_evolution.hpp"
#include "pde_kernel_support.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace {
using namespace penta::pde::kernel;
SpatialSystem spatial(const penta_pde_request_v1& r){
    const auto n=r.nodes;
    const uint64_t c=r.components;
    if(n<3||n>4096||(c!=1&&c!=5)||r.method>1||r.boundary>2||!r.x||!r.initial||!r.reaction)throw std::runtime_error("Invalid one-dimensional implicit PDE layout.");
    for(const auto value:{r.diffusion,r.length})if(!std::isfinite(value))throw std::runtime_error("PDE diffusion/length must be finite.");
    if(r.diffusion<0)throw std::runtime_error("PDE diffusion must be nonnegative.");
    const bool periodic=r.boundary==0,dirichlet=r.boundary==1;
    for(uint64_t i=0;i<n;++i)if(!std::isfinite(r.x[i])||(i&&(!std::isfinite(r.x[i]-r.x[i-1])||r.x[i]<=r.x[i-1])))throw std::runtime_error("PDE coordinates must have finite positive cell widths.");
    const double closing=periodic?r.length-(r.x[n-1]-r.x[0]):0;
    if(periodic&&(!std::isfinite(closing)||closing<=0))throw std::runtime_error("Periodic PDE length must exceed the stored span.");
    if(dirichlet&&(!r.left||!r.right))throw std::runtime_error("Dirichlet endpoint data are required.");
    SpatialSystem system;system.nodes=n;system.components=c;system.identityMass=!r.method;
    system.initial.assign(r.initial,r.initial+n*c);system.weights.assign(n,0);system.fixed.assign(n,0);system.fixedValues.assign(n*c,0);
    if(dirichlet){system.fixed.front()=system.fixed.back()=1;for(uint64_t j=0;j<c;++j){system.fixedValues[j]=r.left[j];system.fixedValues[(n-1)*c+j]=r.right[j];}}
    system.mass.resize(n,n);system.stiffness.resize(n,n);
    std::vector<Eigen::Triplet<double>> mass,stiffness;
    if(r.method){
        const uint64_t edges=periodic?n:n-1;mass.reserve(edges*4);stiffness.reserve(edges*4);
        for(uint64_t edge=0;edge<edges;++edge){
            const auto b=(edge+1)%n;const double width=b?r.x[b]-r.x[edge]:closing;system.weights[edge]+=width/2;system.weights[b]+=width/2;
            for(int i=0;i<2;++i)for(int j=0;j<2;++j){const auto row=i?b:edge,col=j?b:edge;mass.emplace_back(row,col,width*(i==j?2:1)/6);if(r.diffusion)stiffness.emplace_back(row,col,r.diffusion*(i==j?1:-1)/width);}
        }
    }else{
        const double dx=r.x[1]-r.x[0];
        for(uint64_t i=1;i<n;++i)if(std::abs((r.x[i]-r.x[i-1])-dx)>1e-9*dx)throw std::runtime_error("Finite differences require a uniform grid.");
        if(periodic&&std::abs(closing-dx)>1e-9*dx)throw std::runtime_error("Periodic finite-difference length must equal nodes*spacing.");
        const double scale=static_cast<double>(static_cast<long double>(r.diffusion)/dx/dx);
        for(uint64_t i=0;i<n;++i){
            mass.emplace_back(i,i,1);system.weights[i]=dx*((!periodic&&(i==0||i==n-1))?.5:1);
            const auto left=i?i-1:periodic?n-1:1,right=i+1<n?i+1:periodic?0:n-2;
            if(r.diffusion){stiffness.emplace_back(i,i,2*scale);stiffness.emplace_back(i,left,-scale);stiffness.emplace_back(i,right,-scale);}
        }
    }
    system.mass.setFromTriplets(mass.begin(),mass.end());system.stiffness.setFromTriplets(stiffness.begin(),stiffness.end());return system;
}
}
int penta_pde_imex_solve_v1(const penta_pde_host_v1* host,const penta_pde_request_v1* request,penta_pde_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{
        if(!request||!result||request->struct_size<sizeof(penta_pde_time_request_v1)||result->struct_size<sizeof(penta_pde_time_result_v1))throw std::runtime_error("Implicit PDE solve requires the extended time request/result ABI.");
        const auto* extended=reinterpret_cast<const penta_pde_time_request_v1*>(request);
        if(extended->integrator!=PENTA_PDE_IMEX_EULER_V1)throw std::runtime_error("Unsupported implicit PDE integrator.");
        const auto system=spatial(*request);
        TimeSettings settings;settings.integrator=extended->integrator;settings.t0=request->t0;settings.t1=request->t1;settings.step=request->time_step;settings.sample=request->sample_step;settings.maximumWall=request->max_wall_seconds;settings.maximumSteps=request->max_steps;settings.maximumReactions=request->max_reaction_evaluations;
        integrate(host,system,settings,[&](double t,uint64_t i,const double* state,double* output,char* detail,uint64_t size){return request->reaction(request->reaction_context,t,request->x[i],state,output,detail,size);},reinterpret_cast<penta_pde_time_result_v1*>(result));
        return 0;
    }catch(const std::exception& failure){message(error,capacity,failure.what());return 1;}
    catch(...){message(error,capacity,"Unknown implicit PDE failure.");return 1;}
}
