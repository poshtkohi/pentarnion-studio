#include "pde_api.h"
#include "pde_time_api.h"
#include <Eigen/Sparse>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
int penta_pde_imex_solve_v1(const penta_pde_host_v1*,const penta_pde_request_v1*,penta_pde_result_v1*,char*,uint64_t) noexcept;
namespace {
void message(char* p,uint64_t n,const char* t){if(p&&n){const auto k=std::min<uint64_t>(n-1,std::strlen(t));std::memcpy(p,t,k);p[k]=0;}}
template<class Function>
void range(const penta_pde_host_v1* host,uint64_t count,uint64_t cost,Function&& function,uint64_t minimum=64){
    struct Task{
        Function& function;
        static int evaluate(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity)noexcept{
            try{static_cast<Task*>(raw)->function(begin,end);return PENTA_NUMERIC_OK;}
            catch(const std::exception& failure){message(error,capacity,failure.what());return PENTA_NUMERIC_INTERNAL;}
            catch(...){message(error,capacity,"Unknown PDE range failure.");return PENTA_NUMERIC_INTERNAL;}
        }
    } task{function};
    const auto* parallel=host->struct_size>=sizeof(penta_pde_parallel_host_v1)?reinterpret_cast<const penta_pde_parallel_host_v1*>(host):nullptr;
    if(parallel&&parallel->parallel_for&&parallel->threads_granted>1&&count>=minimum){
        char detail[2048]{};
        if(parallel->parallel_for(host->context,count,cost,Task::evaluate,&task,detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"PDE parallel range failed.");
    }else function(0,count);
}
int solve(const penta_pde_host_v1* h,const penta_pde_request_v1* r,penta_pde_result_v1* o,char* error,uint64_t capacity) noexcept {
try{
    if(!h||!r||!o||h->struct_size<sizeof(*h)||h->abi_version!=PENTA_PDE_ABI_V1||r->struct_size<sizeof(*r)||o->struct_size<sizeof(*o))throw std::runtime_error("Incompatible PDE ABI.");
    if(r->struct_size>=sizeof(penta_pde_time_request_v1)){
        const auto integrator=reinterpret_cast<const penta_pde_time_request_v1*>(r)->integrator;
        if(integrator==PENTA_PDE_IMEX_EULER_V1)return penta_pde_imex_solve_v1(h,r,o,error,capacity);
        if(integrator!=PENTA_PDE_RK4_V1)throw std::runtime_error("Unsupported PDE time integrator.");
    }
    if(o->struct_size>=sizeof(penta_pde_time_result_v1)){
        auto* time=reinterpret_cast<penta_pde_time_result_v1*>(o);time->linear_solves=time->factorizations=0;
    }
    const auto n=r->nodes;const auto c=r->components;
    if(n<3||n>4096||(c!=1&&c!=5)||r->method>1||r->boundary>2||!r->x||!r->initial||!r->reaction||!o->times||!o->values||!o->means||!o->spatial_rms)throw std::runtime_error("Invalid PDE layout, method or boundary condition.");
    for(const auto v:{r->diffusion,r->t0,r->t1,r->time_step,r->sample_step,r->length,r->max_wall_seconds})if(!std::isfinite(v))throw std::runtime_error("PDE settings must be finite.");
    if(r->diffusion<0||r->t1<r->t0||r->time_step<=0||r->sample_step<=0||r->max_wall_seconds<=0||!r->max_steps||!r->max_reaction_evaluations)throw std::runtime_error("Invalid PDE time, diffusion or resource settings.");
    const bool periodic=r->boundary==0,dirichlet=r->boundary==1;
    double minH=1e300;
    for(uint64_t i=0;i<n;++i){if(!std::isfinite(r->x[i])||(i&&r->x[i]<=r->x[i-1]))throw std::runtime_error("PDE coordinates must be finite and strictly increasing.");if(i){const double width=r->x[i]-r->x[i-1];if(!std::isfinite(width)||width<=0)throw std::runtime_error("PDE cell widths must be finite and positive.");minH=std::min(minH,width);}}
    const double closing=periodic?r->length-(r->x[n-1]-r->x[0]):0;
    if(periodic&&(!std::isfinite(closing)||closing<=0))throw std::runtime_error("Periodic domain length must exceed the stored node span; omit the repeated endpoint.");
    if(periodic)minH=std::min(minH,closing);
    if(!r->method){const double dx=r->x[1]-r->x[0];for(uint64_t i=1;i<n;++i)if(std::abs((r->x[i]-r->x[i-1])-dx)>1e-9*dx)throw std::runtime_error("Finite differences currently require a uniform grid.");if(periodic&&std::abs(closing-dx)>1e-9*dx)throw std::runtime_error("Periodic finite-difference length must equal nodes*spacing.");}
    if(static_cast<long double>(r->diffusion)*r->time_step/minH/minH>(r->method?0.20:0.60))throw std::runtime_error("Explicit RK4 diffusion stability bound exceeded; reduce time_step.");
    if(dirichlet&&(!r->left||!r->right))throw std::runtime_error("Dirichlet values are required.");
    for(uint64_t i=0;i<n*c;++i)if(!std::isfinite(r->initial[i]))throw std::runtime_error("Initial field must be finite.");
    if(dirichlet)for(uint64_t j=0;j<c;++j)if(!std::isfinite(r->left[j])||!std::isfinite(r->right[j]))throw std::runtime_error("Dirichlet values must be finite.");
    const auto start=std::chrono::steady_clock::now();
    auto check=[&]{if(h->cancelled&&h->cancelled(h->context))throw std::runtime_error("PDE solve cancelled.");if(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>r->max_wall_seconds)throw std::runtime_error("PDE wall-time budget exceeded.");};
    check();
    using Sparse=Eigen::SparseMatrix<double>;Sparse mass(n,n),stiffness(n,n),freeMass;
    Eigen::SimplicialLDLT<Sparse> factor;
    std::vector<double> weights(n,0);
    if(r->method){
        const uint64_t edges=periodic?n:n-1;
        std::vector<Eigen::Triplet<double>> mt(edges*4),kt(edges*4);
        // Retain serial nodal summation and fixed per-element triplet slots so
        // parallel assembly has the same ordering and rounding as serial.
        for(uint64_t e=0;e<edges;++e){const auto b=(e+1)%n;const double dx=b?r->x[b]-r->x[e]:closing;weights[e]+=dx/2;weights[b]+=dx/2;}
        range(h,edges,64,[&](uint64_t begin,uint64_t end){
            for(uint64_t e=begin;e<end;++e){const uint64_t a=e,b=(e+1)%n;const double dx=b?r->x[b]-r->x[a]:closing;
                for(int i=0;i<2;++i)for(int j=0;j<2;++j){const auto row=i?b:a,col=j?b:a,index=e*4+static_cast<uint64_t>(i*2+j);mt[index]=Eigen::Triplet<double>(row,col,dx*(i==j?2:1)/6);kt[index]=Eigen::Triplet<double>(row,col,(i==j?1:-1)/dx);}}
        });
        check();
        mass.setFromTriplets(mt.begin(),mt.end());stiffness.setFromTriplets(kt.begin(),kt.end());
        if(dirichlet){std::vector<Eigen::Triplet<double>> ft;for(int k=0;k<mass.outerSize();++k)for(Sparse::InnerIterator it(mass,k);it;++it)if(it.row()>0&&it.row()<static_cast<int>(n)-1&&it.col()>0&&it.col()<static_cast<int>(n)-1)ft.emplace_back(it.row()-1,it.col()-1,it.value());freeMass.resize(n-2,n-2);freeMass.setFromTriplets(ft.begin(),ft.end());factor.compute(freeMass);}else factor.compute(mass);
        if(factor.info()!=Eigen::Success)throw std::runtime_error("Finite-element mass factorization failed.");
    }else{const double dx=r->x[1]-r->x[0];std::fill(weights.begin(),weights.end(),dx);if(!periodic){weights.front()/=2;weights.back()/=2;}}
    double measure=0;for(const auto w:weights){if(!std::isfinite(w)||w<=0)throw std::runtime_error("PDE quadrature weights must be finite and positive.");measure+=w;}if(!std::isfinite(measure)||measure<=0)throw std::runtime_error("PDE domain measure must be finite and positive.");
    std::vector<double> u(r->initial,r->initial+n*c),f(n*c),k1(n*c),k2(n*c),k3(n*c),k4(n*c),tmp(n*c);
    auto boundary=[&](std::vector<double>& a){if(dirichlet)for(uint64_t j=0;j<c;++j){a[j]=r->left[j];a[(n-1)*c+j]=r->right[j];}};boundary(u);
    auto rhs=[&](double t,const std::vector<double>& a,std::vector<double>& out){check();char chargeError[2048]{};if(h->charge&&h->charge(h->context,n*c,chargeError,sizeof(chargeError)))throw std::runtime_error(chargeError[0]?chargeError:"PDE host budget exceeded.");
        if(o->reaction_evaluations>r->max_reaction_evaluations||n>r->max_reaction_evaluations-o->reaction_evaluations)throw std::runtime_error("PDE reaction evaluation budget exceeded.");
        std::atomic_uint64_t attempted{0};
        try{range(h,n,128,[&](uint64_t begin,uint64_t end){
            uint64_t calls=0;
            try{for(uint64_t i=begin;i<end;++i){char detail[2048]{};++calls;if(r->reaction(r->reaction_context,t,r->x[i],a.data()+i*c,f.data()+i*c,detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"Native PDE reaction failed.");for(uint64_t j=0;j<c;++j)if(!std::isfinite(f[i*c+j]))throw std::runtime_error("Non-finite PDE reaction.");}}
            catch(...){attempted.fetch_add(calls,std::memory_order_relaxed);throw;}
            attempted.fetch_add(calls,std::memory_order_relaxed);
        });}catch(...){o->reaction_evaluations+=attempted.load(std::memory_order_relaxed);throw;}
        o->reaction_evaluations+=attempted.load(std::memory_order_relaxed);
        // The factorization and sparse matrices remain read-only. Eigen's const
        // dense-RHS solve uses destination-local storage, so component solves
        // can share one factorization without creating new decompositions.
        if(r->method){range(h,c,n*16,[&](uint64_t begin,uint64_t end){for(uint64_t j=begin;j<end;++j){Eigen::VectorXd v(n),react(n);for(uint64_t i=0;i<n;++i){v[i]=a[i*c+j];react[i]=f[i*c+j];}Eigen::VectorXd load=mass*react-r->diffusion*(stiffness*v);if(dirichlet){load[0]=load[n-1]=0;Eigen::VectorXd free=load.segment(1,n-2);Eigen::VectorXd d=factor.solve(free);if(factor.info()!=Eigen::Success)throw std::runtime_error("Finite-element mass solve failed.");out[j]=out[(n-1)*c+j]=0;for(uint64_t i=1;i<n-1;++i)out[i*c+j]=d[i-1];}else{Eigen::VectorXd d=factor.solve(load);if(factor.info()!=Eigen::Success)throw std::runtime_error("Finite-element mass solve failed.");for(uint64_t i=0;i<n;++i)out[i*c+j]=d[i];}}},2);}
        else{const double dx=r->x[1]-r->x[0],d=static_cast<double>(static_cast<long double>(r->diffusion)/dx/dx);range(h,n,c*8,[&](uint64_t begin,uint64_t end){for(uint64_t i=begin;i<end;++i)for(uint64_t j=0;j<c;++j){const auto left=i?i-1:periodic?n-1:1,right=i+1<n?i+1:periodic?0:n-2;out[i*c+j]=dirichlet&&(i==0||i==n-1)?0:f[i*c+j]+d*(a[left*c+j]-2*a[i*c+j]+a[right*c+j]);}});}
    };
    auto save=[&](double t){const auto row=o->samples;if(row>=o->sample_capacity||(row+1)>o->value_capacity/(n*c))throw std::runtime_error("PDE retained-field budget exceeded.");o->times[row]=t;std::copy(u.begin(),u.end(),o->values+row*n*c);double rms=0;for(uint64_t j=0;j<c;++j){double mean=0;for(uint64_t i=0;i<n;++i)mean+=weights[i]*u[i*c+j]/measure;o->means[row*c+j]=mean;for(uint64_t i=0;i<n;++i){const double delta=u[i*c+j]-mean;rms+=weights[i]*delta*delta/measure;}}o->spatial_rms[row]=std::sqrt(rms);++o->samples;};
    double t=r->t0;save(t);uint64_t sample=1;
    while(t<r->t1){check();if(o->steps>=r->max_steps)throw std::runtime_error("PDE step budget exceeded.");const double target=std::min(r->t1,r->t0+sample*r->sample_step);if(target<=t){++sample;continue;}const double dt=std::min(r->time_step,target-t);if(t+dt<=t)throw std::runtime_error("PDE time step cannot advance floating-point time.");
        rhs(t,u,k1);for(uint64_t i=0;i<n*c;++i)tmp[i]=u[i]+dt*k1[i]/2;boundary(tmp);rhs(t+dt/2,tmp,k2);for(uint64_t i=0;i<n*c;++i)tmp[i]=u[i]+dt*k2[i]/2;boundary(tmp);rhs(t+dt/2,tmp,k3);for(uint64_t i=0;i<n*c;++i)tmp[i]=u[i]+dt*k3[i];boundary(tmp);rhs(t+dt,tmp,k4);
        for(uint64_t i=0;i<n*c;++i){u[i]+=dt*(k1[i]+2*k2[i]+2*k3[i]+k4[i])/6;if(!std::isfinite(u[i]))throw std::runtime_error("PDE field escaped the finite domain.");}boundary(u);++o->steps;t+=dt;
        if(t>=target-16*std::numeric_limits<double>::epsilon()*std::max(1.0,std::abs(target))){t=target;save(t);++sample;}
    }
    o->elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();message(o->status,sizeof(o->status),"completed");return 0;
}catch(const std::exception& e){message(error,capacity,e.what());return 1;}catch(...){message(error,capacity,"Unknown native PDE failure.");return 1;}}
const penta_pde_parallel_api_v1 api{{sizeof(penta_pde_parallel_api_v1),PENTA_PDE_ABI_V1,solve},PENTA_PDE_CAP_PARALLEL_V1|PENTA_PDE_CAP_IMEX_EULER_V1};
}
#ifdef _WIN32
#define PENTA_PDE_EXPORT __declspec(dllexport)
#else
#define PENTA_PDE_EXPORT __attribute__((visibility("default")))
#endif
extern "C" PENTA_PDE_EXPORT const penta_pde_api_v1* penta_pde_query_v1(uint32_t version){return version==PENTA_PDE_ABI_V1?&api.base:nullptr;}
