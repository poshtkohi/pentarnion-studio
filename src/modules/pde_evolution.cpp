#include "pde_evolution.hpp"
#include "pde_kernel_support.hpp"
#include <Eigen/SparseLU>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace penta::pde::kernel {
void integrate(const penta_pde_host_v1* host,const SpatialSystem& system,const TimeSettings& settings,const NodeReaction& evaluate,penta_pde_time_result_v1* report){
    if(!host||host->struct_size<sizeof(penta_pde_host_v1)||host->abi_version!=PENTA_PDE_ABI_V1||!report||report->base.struct_size<sizeof(*report))throw std::runtime_error("Incompatible PDE time ABI.");
    const auto n=system.nodes,c=system.components,total=n*c;auto& out=report->base;
    if(!n||n>16384||(c!=1&&c!=5)||system.initial.size()!=total||system.weights.size()!=n||system.fixed.size()!=n||system.fixedValues.size()!=total||!evaluate||!out.times||!out.values||!out.means||!out.spatial_rms)throw std::runtime_error("Invalid PDE spatial/time layout.");
    for(const auto number:{settings.t0,settings.t1,settings.step,settings.sample,settings.maximumWall})if(!std::isfinite(number))throw std::runtime_error("PDE time settings must be finite.");
    if(settings.t1<settings.t0||settings.step<=0||settings.sample<=0||settings.maximumWall<=0||!settings.maximumSteps||!settings.maximumReactions||settings.integrator>1)throw std::runtime_error("Invalid PDE time method or budget.");
    if(system.mass.rows()!=static_cast<Eigen::Index>(n)||system.mass.cols()!=static_cast<Eigen::Index>(n)||system.stiffness.rows()!=static_cast<Eigen::Index>(n)||system.stiffness.cols()!=static_cast<Eigen::Index>(n))throw std::runtime_error("PDE operator shape mismatch.");
    for(const auto* matrix:{&system.mass,&system.stiffness})for(int column=0;column<matrix->outerSize();++column)for(Eigen::SparseMatrix<double>::InnerIterator item(*matrix,column);item;++item)if(!std::isfinite(item.value()))throw std::runtime_error("PDE spatial operators must have finite entries.");
    if(settings.integrator==PENTA_PDE_RK4_V1&&!system.identityMass)throw std::runtime_error("This spatial backend requires implicit diffusion for a nonidentity mass.");
    double measure=0;
    for(uint64_t i=0;i<n;++i){if(!std::isfinite(system.weights[i])||system.weights[i]<=0)throw std::runtime_error("PDE quadrature weights must be finite and positive.");measure+=system.weights[i];}
    if(!std::isfinite(measure)||measure<=0)throw std::runtime_error("PDE domain measure is invalid.");
    for(uint64_t i=0;i<total;++i)if(!std::isfinite(system.initial[i])||!std::isfinite(system.fixedValues[i]))throw std::runtime_error("PDE initial/boundary values must be finite.");
    const auto started=std::chrono::steady_clock::now();
    auto check=[&]{if(host->cancelled&&host->cancelled(host->context))throw std::runtime_error("PDE solve cancelled.");if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>settings.maximumWall)throw std::runtime_error("PDE wall-time budget exceeded.");};
    check();
    out.samples=out.steps=out.reaction_evaluations=0;report->linear_solves=report->factorizations=0;
    std::vector<double> u=system.initial,force(total),temporary(total),k1(total),k2(total),k3(total),k4(total);
    auto boundary=[&](std::vector<double>& values){for(uint64_t i=0;i<n;++i)if(system.fixed[i])for(uint64_t j=0;j<c;++j)values[i*c+j]=system.fixedValues[i*c+j];};
    boundary(u);
    auto reaction=[&](double t,const std::vector<double>& state){
        check();char detail[2048]{};
        if(host->charge&&host->charge(host->context,total,detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"PDE host budget exceeded.");
        if(out.reaction_evaluations>settings.maximumReactions||n>settings.maximumReactions-out.reaction_evaluations)throw std::runtime_error("PDE reaction evaluation budget exceeded.");
        std::atomic_uint64_t attempted{0};
        try{range(host,n,128,[&](uint64_t begin,uint64_t end){uint64_t calls=0;try{for(uint64_t i=begin;i<end;++i){char error[2048]{};++calls;if(evaluate(t,i,state.data()+i*c,force.data()+i*c,error,sizeof(error)))throw std::runtime_error(error[0]?error:"Native PDE reaction failed.");for(uint64_t j=0;j<c;++j)if(!std::isfinite(force[i*c+j]))throw std::runtime_error("Non-finite PDE reaction.");}}catch(...){attempted.fetch_add(calls,std::memory_order_relaxed);throw;}attempted.fetch_add(calls,std::memory_order_relaxed);});}
        catch(...){out.reaction_evaluations+=attempted.load(std::memory_order_relaxed);throw;}
        out.reaction_evaluations+=attempted.load(std::memory_order_relaxed);
    };
    Eigen::SparseMatrix<double,Eigen::RowMajor> rowStiffness=system.stiffness;
    auto rhs=[&](double t,const std::vector<double>& state,std::vector<double>& output){
        reaction(t,state);
        range(host,n,c*10,[&](uint64_t begin,uint64_t end){for(uint64_t i=begin;i<end;++i)for(uint64_t j=0;j<c;++j){double diffusion=0;for(Eigen::SparseMatrix<double,Eigen::RowMajor>::InnerIterator item(rowStiffness,static_cast<int>(i));item;++item)diffusion+=item.value()*state[static_cast<uint64_t>(item.col())*c+j];output[i*c+j]=system.fixed[i]?0:force[i*c+j]-diffusion;}});
    };
    std::vector<int> freeMap(n,-1);std::vector<uint64_t> freeNodes;
    for(uint64_t i=0;i<n;++i)if(!system.fixed[i]){freeMap[i]=static_cast<int>(freeNodes.size());freeNodes.push_back(i);}
    if(freeNodes.empty())throw std::runtime_error("PDE grid has no free degrees of freedom.");
    using Sparse=Eigen::SparseMatrix<double>;
    Eigen::SparseLU<Sparse,Eigen::COLAMDOrdering<int>> factor;
    double factorKey=std::numeric_limits<double>::quiet_NaN();bool analyzed=false;
    std::vector<double> correction(freeNodes.size()*c,0);
    auto implicitStep=[&](double t,double dt){
        reaction(t,u);check();
        const double key=system.stiffness.nonZeros()?dt:0;
        if(key!=factorKey){
            Sparse combined=system.mass+dt*system.stiffness;
            for(int column=0;column<combined.outerSize();++column)for(Sparse::InnerIterator item(combined,column);item;++item)if(!std::isfinite(item.value()))throw std::runtime_error("PDE implicit diffusion operator overflowed.");
            std::vector<Eigen::Triplet<double>> triplets;triplets.reserve(static_cast<std::size_t>(combined.nonZeros()));
            std::fill(correction.begin(),correction.end(),0);
            for(int column=0;column<combined.outerSize();++column)for(Sparse::InnerIterator item(combined,column);item;++item){
                const auto row=static_cast<uint64_t>(item.row()),col=static_cast<uint64_t>(item.col());
                if(freeMap[row]<0)continue;
                if(freeMap[col]>=0)triplets.emplace_back(freeMap[row],freeMap[col],item.value());
                else for(uint64_t j=0;j<c;++j)correction[static_cast<std::size_t>(freeMap[row])*c+j]+=item.value()*system.fixedValues[col*c+j];
            }
            Sparse freeOperator(static_cast<int>(freeNodes.size()),static_cast<int>(freeNodes.size()));freeOperator.setFromTriplets(triplets.begin(),triplets.end());
            if(!analyzed){factor.analyzePattern(freeOperator);analyzed=true;}
            factor.factorize(freeOperator);if(factor.info()!=Eigen::Success)throw std::runtime_error("Implicit PDE diffusion factorization failed.");
            ++report->factorizations;factorKey=key;check();
        }
        // One sparse factorization is reused across every coefficient. These
        // solves remain serial; spatial reactions/operators use the shared pool.
        for(uint64_t j=0;j<c;++j){
            Eigen::VectorXd predictor(n);for(uint64_t i=0;i<n;++i)predictor[i]=u[i*c+j]+dt*force[i*c+j];
            const Eigen::VectorXd full=system.mass*predictor;Eigen::VectorXd load(freeNodes.size());
            for(std::size_t i=0;i<freeNodes.size();++i)load[static_cast<Eigen::Index>(i)]=full[freeNodes[i]]-correction[i*c+j];
            const Eigen::VectorXd solved=factor.solve(load);if(factor.info()!=Eigen::Success)throw std::runtime_error("Implicit PDE diffusion solve failed.");
            ++report->linear_solves;
            for(std::size_t i=0;i<freeNodes.size();++i)temporary[freeNodes[i]*c+j]=solved[static_cast<Eigen::Index>(i)];
        }
        boundary(temporary);u.swap(temporary);
    };
    auto save=[&](double t){
        const auto row=out.samples;if(row>=out.sample_capacity||(row+1)>out.value_capacity/total)throw std::runtime_error("PDE retained-field budget exceeded.");
        out.times[row]=t;std::copy(u.begin(),u.end(),out.values+row*total);double rms=0;
        for(uint64_t j=0;j<c;++j){double mean=0;for(uint64_t i=0;i<n;++i)mean+=system.weights[i]*u[i*c+j]/measure;out.means[row*c+j]=mean;for(uint64_t i=0;i<n;++i){const double delta=u[i*c+j]-mean;rms+=system.weights[i]*delta*delta/measure;}}
        out.spatial_rms[row]=std::sqrt(rms);++out.samples;
    };
    double t=settings.t0;save(t);uint64_t sample=1;
    while(t<settings.t1){
        check();if(out.steps>=settings.maximumSteps)throw std::runtime_error("PDE step budget exceeded.");
        const double target=std::min(settings.t1,settings.t0+sample*settings.sample);if(target<=t){++sample;continue;}
        const double dt=std::min(settings.step,target-t);if(t+dt<=t)throw std::runtime_error("PDE time step cannot advance floating-point time.");
        if(settings.integrator==PENTA_PDE_IMEX_EULER_V1)implicitStep(t,dt);
        else{
            rhs(t,u,k1);for(uint64_t i=0;i<total;++i)temporary[i]=u[i]+dt*k1[i]/2;boundary(temporary);
            rhs(t+dt/2,temporary,k2);for(uint64_t i=0;i<total;++i)temporary[i]=u[i]+dt*k2[i]/2;boundary(temporary);
            rhs(t+dt/2,temporary,k3);for(uint64_t i=0;i<total;++i)temporary[i]=u[i]+dt*k3[i];boundary(temporary);
            rhs(t+dt,temporary,k4);for(uint64_t i=0;i<total;++i)u[i]+=dt*(k1[i]+2*k2[i]+2*k3[i]+k4[i])/6;boundary(u);
        }
        for(const auto value:u)if(!std::isfinite(value))throw std::runtime_error("PDE field escaped the finite domain.");
        ++out.steps;t+=dt;
        if(t>=target-16*std::numeric_limits<double>::epsilon()*std::max(1.0,std::abs(target))){t=target;save(t);++sample;}
    }
    out.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();message(out.status,sizeof(out.status),"completed");
}
}
