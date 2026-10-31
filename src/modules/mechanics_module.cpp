#include "mechanics_api.h"
#include <Eigen/LU>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
void message(char* out,uint64_t cap,const char* text){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
int solve(const penta_mechanics_host_v1* host,const penta_mechanics_request_v1* request,penta_mechanics_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_MECHANICS_ABI_V1||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw std::runtime_error("Incompatible mechanics ABI.");
        const auto& r=*request;auto& out=*result;const unsigned n=r.dimension,m=1+2*n;
        if(!n||n>8||r.form>2||r.method>2||r.position_only>1||!r.initial_q||!r.initial_second||!r.model||!out.times||!out.q||!out.velocity||!out.momentum||!out.energy||!out.model_value||!out.kinetic||!r.max_steps||r.max_steps>10000000||!r.max_evaluations||r.max_evaluations>10000000||!r.max_newton_iterations||r.max_newton_iterations>100)throw std::runtime_error("Invalid mechanics dimensions, buffers or budgets.");
        if((r.method==PENTA_MECH_VERLET&&(r.form!=PENTA_MECH_NEWTON||!r.position_only))||(r.method==PENTA_MECH_MIDPOINT&&r.form!=PENTA_MECH_HAMILTONIAN)||(r.force&&r.form!=PENTA_MECH_LAGRANGIAN))throw std::runtime_error("Verlet requires Newton position-only forces; midpoint requires a Hamiltonian; generalized force applies to a Lagrangian.");
        for(const auto value:{r.t0,r.t1,r.time_step,r.sample_step,r.max_wall_seconds,r.newton_tolerance,r.regularity_tolerance})if(!std::isfinite(value))throw std::runtime_error("Mechanics settings must be finite.");
        if(r.t1<r.t0||r.time_step<=0||r.sample_step<=0||r.max_wall_seconds<=0||r.newton_tolerance<=0||r.regularity_tolerance<=0||r.regularity_tolerance>=.1)throw std::runtime_error("Invalid mechanics time/tolerance settings.");
        for(unsigned j=0;j<n;++j){if(!std::isfinite(r.initial_q[j])||!std::isfinite(r.initial_second[j]))throw std::runtime_error("Mechanics initial coordinates must be finite.");if(r.form==0&&(!r.masses||!std::isfinite(r.masses[j])||r.masses[j]<=0))throw std::runtime_error("Newton masses must be finite positive constants, one per real coordinate.");}
        const auto started=std::chrono::steady_clock::now();out.samples=out.steps=out.evaluations=out.newton_iterations=out.force_evaluations=0;out.energy_defined=r.form!=0;out.max_relative_energy_drift=0;
        auto check=[&]{if(host->cancelled&&host->cancelled(host->context))throw std::runtime_error("Mechanics solve cancelled.");if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>r.max_wall_seconds)throw std::runtime_error("Mechanics wall-time budget exceeded.");};
        std::vector<double> state(2*n),value(std::max(n,1u)),gradient(m),hessian(m*m),force(n);std::copy_n(r.initial_q,n,state.data());std::copy_n(r.initial_second,n,state.data()+n);
        auto evaluate=[&](double t,const std::vector<double>& y,unsigned order){check();if(out.evaluations>=r.max_evaluations)throw std::runtime_error("Mechanics model evaluation budget exceeded.");char detail[2048]{};if(host->charge&&host->charge(host->context,m*(order+1),detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"Mechanics native work budget exceeded.");++out.evaluations;if(r.model(r.model_context,t,y.data(),y.data()+n,order,value.data(),order?gradient.data():nullptr,order==2?hessian.data():nullptr,detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"Compiled mechanics model failed.");for(unsigned j=0;j<(r.form==0?n:1);++j)if(!std::isfinite(value[j]))throw std::runtime_error("Non-finite mechanics model output.");if(order)for(const auto x:gradient)if(!std::isfinite(x))throw std::runtime_error("Non-finite generated mechanics gradient.");if(order==2)for(const auto x:hessian)if(!std::isfinite(x))throw std::runtime_error("Non-finite generated mechanics Hessian.");};
        auto rhs=[&](double t,const std::vector<double>& y,std::vector<double>& derivative,Eigen::MatrixXd* jacobian=nullptr){
            derivative.resize(2*n);evaluate(t,y,r.form==0?0:r.form==1||jacobian?2:1);
            if(r.form==0){for(unsigned j=0;j<n;++j){derivative[j]=y[n+j];derivative[n+j]=value[j]/r.masses[j];}}
            else if(r.form==2){for(unsigned j=0;j<n;++j){derivative[j]=gradient[1+n+j];derivative[n+j]=-gradient[1+j];}if(jacobian){jacobian->resize(2*n,2*n);for(unsigned row=0;row<2*n;++row)for(unsigned column=0;column<2*n;++column)(*jacobian)(row,column)=(row<n?1:-1)*hessian[(row<n?1+n+row:1+row-n)*m+1+column];}}
            else{
                std::fill(force.begin(),force.end(),0);if(r.force){if(out.force_evaluations>=r.max_evaluations)throw std::runtime_error("Generalized force evaluation budget exceeded.");++out.force_evaluations;char detail[2048]{};if(r.force(r.force_context,t,y.data(),y.data()+n,force.data(),detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"Compiled generalized force failed.");}
                Eigen::MatrixXd mass(n,n);Eigen::VectorXd load(n);
                for(unsigned row=0;row<n;++row){if(!std::isfinite(force[row]))throw std::runtime_error("Non-finite generalized force.");derivative[row]=y[n+row];double f=gradient[1+row]+force[row]-hessian[(1+n+row)*m];for(unsigned column=0;column<n;++column){mass(row,column)=hessian[(1+n+row)*m+1+n+column];f-=hessian[(1+n+row)*m+1+column]*y[n+column];}load[row]=f;}
                Eigen::FullPivLU<Eigen::MatrixXd> factor(mass);factor.setThreshold(r.regularity_tolerance);if(factor.rank()!=n)throw std::runtime_error("Singular Lagrangian velocity Hessian: constrained/degenerate mechanics requires a DAE formulation.");const Eigen::VectorXd acceleration=factor.solve(load);if(!acceleration.allFinite()||(mass*acceleration-load).norm()>1e-8*(1+load.norm()))throw std::runtime_error("Lagrangian mass solve failed its residual check.");for(unsigned j=0;j<n;++j)derivative[n+j]=acceleration[j];
            }
        };
        double initialEnergy=0;
        auto save=[&](double t){if(out.samples>=out.capacity)throw std::runtime_error("Mechanics retained-sample budget exceeded.");const auto row=out.samples;out.times[row]=t;std::copy_n(state.data(),n,out.q+row*n);double energy=0,kinetic=0;
            if(r.form==0){for(unsigned j=0;j<n;++j){out.velocity[row*n+j]=state[n+j];out.momentum[row*n+j]=r.masses[j]*state[n+j];kinetic+=.5*r.masses[j]*state[n+j]*state[n+j];}out.model_value[row]=0;}
            else{evaluate(t,state,1);out.model_value[row]=value[0];if(r.form==2){energy=value[0];for(unsigned j=0;j<n;++j){out.velocity[row*n+j]=gradient[1+n+j];out.momentum[row*n+j]=state[n+j];}}else{energy=-value[0];for(unsigned j=0;j<n;++j){out.velocity[row*n+j]=state[n+j];out.momentum[row*n+j]=gradient[1+n+j];energy+=state[n+j]*gradient[1+n+j];}}}
            if(!std::isfinite(energy)||!std::isfinite(kinetic))throw std::runtime_error("Mechanics energy diagnostic overflowed.");
            out.energy[row]=energy;out.kinetic[row]=kinetic;if(!row)initialEnergy=energy;if(out.energy_defined)out.max_relative_energy_drift=std::max(out.max_relative_energy_drift,std::abs(energy-initialEnergy)/std::max(1.0,std::abs(initialEnergy)));++out.samples;
        };
        double t=r.t0;if(r.form==PENTA_MECH_LAGRANGIAN){std::vector<double> initialDerivative;rhs(t,state,initialDerivative);}save(t);uint64_t sampleIndex=1;std::vector<double>a,b,c,d,trial(2*n),next(2*n);
        while(t<r.t1){check();if(out.steps>=r.max_steps)throw std::runtime_error("Mechanics step budget exceeded.");const double target=std::min(r.t1,r.t0+sampleIndex*r.sample_step);if(target<=t){++sampleIndex;continue;}const double dt=std::min(r.time_step,target-t);if(t+dt<=t)throw std::runtime_error("Mechanics step cannot advance time.");
            if(r.method==0){rhs(t,state,a);for(unsigned j=0;j<2*n;++j)trial[j]=state[j]+dt*a[j]/2;rhs(t+dt/2,trial,b);for(unsigned j=0;j<2*n;++j)trial[j]=state[j]+dt*b[j]/2;rhs(t+dt/2,trial,c);for(unsigned j=0;j<2*n;++j)trial[j]=state[j]+dt*c[j];rhs(t+dt,trial,d);for(unsigned j=0;j<2*n;++j)next[j]=state[j]+dt*(a[j]+2*b[j]+2*c[j]+d[j])/6;}
            else if(r.method==1){rhs(t,state,a);for(unsigned j=0;j<n;++j){next[n+j]=state[n+j]+dt*a[n+j]/2;next[j]=state[j]+dt*next[n+j];}rhs(t+dt,next,b);for(unsigned j=0;j<n;++j)next[n+j]+=dt*b[n+j]/2;}
            else{
                rhs(t,state,a);for(unsigned j=0;j<2*n;++j)next[j]=state[j]+dt*a[j];bool converged=false;
                for(unsigned iteration=0;iteration<r.max_newton_iterations;++iteration){++out.newton_iterations;for(unsigned j=0;j<2*n;++j)trial[j]=.5*(state[j]+next[j]);Eigen::MatrixXd jac;rhs(t+dt/2,trial,b,&jac);Eigen::VectorXd residual(2*n);double scale=1;for(unsigned j=0;j<2*n;++j){residual[j]=next[j]-state[j]-dt*b[j];scale=std::max(scale,std::max(std::abs(state[j]),std::abs(next[j])));}if(residual.lpNorm<Eigen::Infinity>()<=r.newton_tolerance*scale){converged=true;break;}const Eigen::MatrixXd matrix=Eigen::MatrixXd::Identity(2*n,2*n)-dt*jac/2;Eigen::FullPivLU<Eigen::MatrixXd> factor(matrix);factor.setThreshold(r.regularity_tolerance);if(factor.rank()!=2*n)throw std::runtime_error("Implicit midpoint Newton matrix is singular.");const Eigen::VectorXd update=factor.solve(residual);if(!update.allFinite())throw std::runtime_error("Implicit midpoint Newton update is non-finite.");for(unsigned j=0;j<2*n;++j)next[j]-=update[j];}
                if(!converged)throw std::runtime_error("Implicit midpoint Newton iteration limit exceeded; reduce time_step.");
            }
            for(const auto x:next)if(!std::isfinite(x))throw std::runtime_error("Mechanics state escaped the finite domain.");
            state.swap(next);t+=dt;++out.steps;if(t>=target-16*std::numeric_limits<double>::epsilon()*std::max(1.0,std::abs(target))){t=target;save(t);++sampleIndex;}
        }
        out.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();return 0;
    }catch(const std::exception& failure){message(error,capacity,failure.what());return 1;}catch(...){message(error,capacity,"Unknown native mechanics failure.");return 4;}
}
const penta_mechanics_api_v1 api{sizeof(api),PENTA_MECHANICS_ABI_V1,solve};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_mechanics_api_v1*penta_mechanics_query_v1(uint32_t version){return version==PENTA_MECHANICS_ABI_V1?&api:nullptr;}
