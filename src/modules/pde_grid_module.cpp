#include "pde_grid_api.h"
#include "pde_evolution.hpp"
#include "pde_kernel_support.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace {
using namespace penta::pde::kernel;
double spacing(const double* x,uint64_t n,double length,bool periodic){
    if(!x||n<3||n>256||!std::isfinite(length))throw std::runtime_error("PDE grid axes require 3..256 coordinates and finite lengths.");
    const double dx=x[1]-x[0];if(!std::isfinite(dx)||dx<=0)throw std::runtime_error("PDE grid spacing must be finite and positive.");
    for(uint64_t i=0;i<n;++i)if(!std::isfinite(x[i])||(i&&(!std::isfinite(x[i]-x[i-1])||std::abs((x[i]-x[i-1])-dx)>1e-9*dx)))throw std::runtime_error("Two-dimensional finite differences require increasing uniform axes.");
    const double expected=dx*static_cast<double>(periodic?n:n-1);
    if(!std::isfinite(expected)||std::abs(length-expected)>1e-9*std::max(expected,dx))throw std::runtime_error("PDE grid lengths must match the explicit axes, omitting periodic duplicate endpoints.");
    return dx;
}
int solve(const penta_pde_host_v1* host,const penta_pde_grid_request_v1* request,penta_pde_time_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{
        if(!request||request->struct_size<sizeof(*request)||request->abi_version!=PENTA_PDE_GRID_ABI_V1||!result||result->base.struct_size<sizeof(*result))throw std::runtime_error("Incompatible structured-grid PDE ABI.");
        const auto& r=*request;const uint64_t nx=r.nx,ny=r.ny,c=r.components;
        if(nx<3||ny<3||nx>256||ny>256||nx>16384/ny||(c!=1&&c!=5)||r.boundary>2||r.integrator>1||!r.initial||!r.reaction)throw std::runtime_error("PDE2 supports 3..256 nodes per axis, at most16384 nodes, real/Pentarnion states and supported methods/boundaries.");
        const auto n=nx*ny;const bool periodic=r.boundary==0,dirichlet=r.boundary==1;
        const double dx=spacing(r.x,nx,r.length_x,periodic),dy=spacing(r.y,ny,r.length_y,periodic);
        for(const auto value:{r.diffusion_x,r.diffusion_y})if(!std::isfinite(value)||value<0)throw std::runtime_error("PDE grid diffusion coefficients must be finite and nonnegative.");
        if(dirichlet&&!r.boundary_value)throw std::runtime_error("PDE2 constant Dirichlet data are required.");
        if(r.integrator==PENTA_PDE_RK4_V1&&static_cast<long double>(r.time_step)*(static_cast<long double>(r.diffusion_x)/dx/dx+static_cast<long double>(r.diffusion_y)/dy/dy)>.60L)throw std::runtime_error("Explicit PDE2 RK4 diffusion bound exceeded; reduce time_step or select imex_euler.");
        SpatialSystem system;system.nodes=n;system.components=c;system.identityMass=true;system.initial.assign(r.initial,r.initial+n*c);system.weights.resize(n);system.fixed.assign(n,0);system.fixedValues.assign(n*c,0);system.mass.resize(n,n);system.mass.setIdentity();system.stiffness.resize(n,n);
        const double sx=static_cast<double>(static_cast<long double>(r.diffusion_x)/dx/dx),sy=static_cast<double>(static_cast<long double>(r.diffusion_y)/dy/dy);
        std::vector<Eigen::Triplet<double>> entries;entries.reserve(n*5);
        for(uint64_t iy=0;iy<ny;++iy)for(uint64_t ix=0;ix<nx;++ix){
            const uint64_t i=iy*nx+ix;
            system.weights[i]=dx*dy*((!periodic&&(ix==0||ix+1==nx))?.5:1)*((!periodic&&(iy==0||iy+1==ny))?.5:1);
            if(dirichlet&&(ix==0||ix+1==nx||iy==0||iy+1==ny)){system.fixed[i]=1;for(uint64_t j=0;j<c;++j)system.fixedValues[i*c+j]=r.boundary_value[j];}
            const auto left=ix?ix-1:periodic?nx-1:1,right=ix+1<nx?ix+1:periodic?0:nx-2;
            const auto below=iy?iy-1:periodic?ny-1:1,above=iy+1<ny?iy+1:periodic?0:ny-2;
            if(sx||sy)entries.emplace_back(i,i,2*(sx+sy));
            if(sx){entries.emplace_back(i,iy*nx+left,-sx);entries.emplace_back(i,iy*nx+right,-sx);}
            if(sy){entries.emplace_back(i,below*nx+ix,-sy);entries.emplace_back(i,above*nx+ix,-sy);}
        }
        system.stiffness.setFromTriplets(entries.begin(),entries.end());
        TimeSettings settings;settings.integrator=r.integrator;settings.t0=r.t0;settings.t1=r.t1;settings.step=r.time_step;settings.sample=r.sample_step;settings.maximumWall=r.max_wall_seconds;settings.maximumSteps=r.max_steps;settings.maximumReactions=r.max_reaction_evaluations;
        integrate(host,system,settings,[&](double t,uint64_t i,const double* state,double* output,char* detail,uint64_t size){return r.reaction(r.reaction_context,t,r.x[i%nx],r.y[i/nx],state,output,detail,size);},result);
        return 0;
    }catch(const std::exception& failure){message(error,capacity,failure.what());return 1;}
    catch(...){message(error,capacity,"Unknown structured-grid PDE failure.");return 1;}
}
const penta_pde_grid_api_v1 api{sizeof(api),PENTA_PDE_GRID_ABI_V1,solve};
}
#ifdef _WIN32
#define PENTA_GRID_EXPORT __declspec(dllexport)
#else
#define PENTA_GRID_EXPORT __attribute__((visibility("default")))
#endif
extern "C" PENTA_GRID_EXPORT const penta_pde_grid_api_v1* penta_pde_grid_query_v1(uint32_t version){return version==PENTA_PDE_GRID_ABI_V1?&api:nullptr;}
