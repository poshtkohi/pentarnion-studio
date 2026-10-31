#include "orbits_api.h"
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Vector=Eigen::VectorXd;
using Matrix=Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic,Eigen::RowMajor>;
using Clock=std::chrono::steady_clock;
void text(char* out,uint64_t capacity,const char* message){if(out&&capacity){const auto count=std::min<uint64_t>(capacity-1,std::strlen(message));std::memcpy(out,message,count);out[count]=0;}}
struct Guard {
    const penta_orbits_host_v1* host;Clock::time_point start=Clock::now();double seconds;
    void check(uint64_t work=1)const{
        if(host->cancelled&&host->cancelled(host->context))throw std::runtime_error("Native orbit analysis cancelled.");
        if(std::chrono::duration<double>(Clock::now()-start).count()>seconds)throw std::runtime_error("Native orbit analysis wall-time budget reached.");
        char error[512]{};if(host->charge&&host->charge(host->context,work,error,sizeof(error)))throw std::runtime_error(error[0]?error:"Native orbit host budget reached.");
    }
};
void hostValid(const penta_orbits_host_v1* h){if(!h||h->struct_size<sizeof(*h)||h->abi_version!=PENTA_ORBITS_ABI_V1)throw std::runtime_error("Incompatible native orbit host ABI.");}
double norm(const Vector& x){return x.lpNorm<Eigen::Infinity>();}
Vector newtonStep(const Matrix& jacobian,const Vector& residual){
    Eigen::FullPivLU<Matrix> factor(jacobian);if(factor.rank()!=jacobian.cols())throw std::runtime_error("Orbit corrector Jacobian is singular or rank deficient.");
    Vector step=factor.solve(-residual);if(!step.allFinite())throw std::runtime_error("Orbit corrector produced nonfinite coordinates.");
    const double error=norm(jacobian*step+residual);
    if(error>1e-7*std::max(1.0,norm(residual)))throw std::runtime_error("Orbit corrector linear residual check failed.");
    return step;
}
int periodic(const penta_orbits_host_v1* host,penta_orbits_periodic_v1* r,char* error,uint64_t capacity)noexcept{
    try{
        hostValid(host);
        if(!r||r->struct_size<sizeof(*r)||r->dimension<2||r->dimension>32||!r->initial||!r->phase_reference||!r->phase_direction||!r->state_scale||!r->flow||!r->state||!r->monodromy||!r->history||r->max_iterations<1||r->max_iterations>100||r->history_capacity<r->max_iterations+1)throw std::runtime_error("Invalid periodic-orbit request/buffers.");
        if(!std::isfinite(r->period)||!std::isfinite(r->min_period)||!std::isfinite(r->max_period)||!std::isfinite(r->tolerance)||!std::isfinite(r->max_seconds)||r->min_period<=0||r->max_period<=r->min_period||r->period<r->min_period||r->period>r->max_period||r->tolerance<=0||r->max_seconds<=0||!std::isfinite(r->period_scale)||r->period_scale<=0)throw std::runtime_error("Invalid periodic-orbit bounds/tolerance/budget.");
        const auto n=static_cast<Eigen::Index>(r->dimension);Guard guard{host,Clock::now(),r->max_seconds};
        const Vector scale=Eigen::Map<const Vector>(r->state_scale,n);if(!scale.allFinite()||(scale.array()<=0).any())throw std::runtime_error("State scales must be finite and positive.");
        const Vector reference=Eigen::Map<const Vector>(r->phase_reference,n);Vector direction=Eigen::Map<const Vector>(r->phase_direction,n),state=Eigen::Map<const Vector>(r->initial,n);
        if(!reference.allFinite()||!direction.allFinite()||!state.allFinite()||direction.stableNorm()<1e-14)throw std::runtime_error("Periodic shooting needs finite states and a nonzero phase direction.");
        direction=direction.cwiseQuotient(scale);
        const double directionScale=direction.cwiseAbs().maxCoeff();
        if(!direction.allFinite()||!(directionScale>0))throw std::runtime_error("Scaled periodic phase direction must be finite and nonzero.");
        // Normalize a bounded vector so neither squared magnitudes nor the
        // representable norm of a large phase direction can erase the section.
        direction/=directionScale;direction.normalize();
        auto phase=[&](const Vector& x){
            long double value=0;
            for(Eigen::Index k=0;k<n;++k)value+=static_cast<long double>(direction[k])*(static_cast<long double>(x[k])-reference[k])/scale[k];
            if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<double>::max())throw std::runtime_error("Periodic phase residual exceeds the finite real range.");
            return static_cast<double>(value);
        };
        double period=r->period;Vector endpoint(n),finalRhs(n),residual(n+1);Matrix monodromy(n,n);r->history_rows=r->iterations=r->evaluations=0;r->converged=0;
        auto evaluate=[&](const Vector& x,double time,bool tangent,Vector& end,Matrix& phi,Vector& rhs){guard.check(static_cast<uint64_t>(n*n));char failure[1024]{};++r->evaluations;if(r->flow(r->context,x.data(),time,tangent?1:0,end.data(),phi.data(),rhs.data(),failure,sizeof(failure)))throw std::runtime_error(failure[0]?failure:"Native periodic flow evaluation failed.");if(!end.allFinite()||!rhs.allFinite()||(tangent&&!phi.allFinite()))throw std::runtime_error("Periodic shooting returned nonfinite flow/tangent values.");};
        double lastStep=0,lastAlpha=0;
        for(uint64_t iteration=0;iteration<=r->max_iterations;++iteration){
            evaluate(state,period,true,endpoint,monodromy,finalRhs);const Vector closure=endpoint-state;residual.head(n)=closure.cwiseQuotient(scale);residual[n]=phase(state);
            if(!closure.allFinite()||!residual.allFinite())throw std::runtime_error("Periodic closure/phase residual exceeds the finite real range.");
            r->closure_residual=norm(closure);r->scaled_closure_residual=norm(residual.head(n));r->phase_residual=std::abs(residual[n]);r->iterations=iteration;
            double* row=r->history+r->history_rows*6;row[0]=static_cast<double>(iteration);row[1]=period;row[2]=r->scaled_closure_residual;row[3]=r->phase_residual;row[4]=lastStep;row[5]=lastAlpha;++r->history_rows;
            if(norm(residual)<=r->tolerance){r->converged=1;text(r->status,sizeof(r->status),"periodic_candidate");break;}
            if(iteration==r->max_iterations){text(r->status,sizeof(r->status),"maximum_iterations");break;}
            Matrix jacobian=Matrix::Zero(n+1,n+1);jacobian.topLeftCorner(n,n)=monodromy-Matrix::Identity(n,n);jacobian.topRightCorner(n,1)=finalRhs;jacobian.bottomLeftCorner(1,n)=direction.transpose();
            for(Eigen::Index row=0;row<n;++row){for(Eigen::Index col=0;col<n;++col)jacobian(row,col)*=scale[col]/scale[row];jacobian(row,n)*=r->period_scale/scale[row];}
            Vector step;
            try{step=newtonStep(jacobian,residual);}catch(const std::exception& e){text(r->status,sizeof(r->status),"singular_corrector");text(r->message,sizeof(r->message),e.what());break;}
            bool accepted=false;double alpha=1;Vector trialEnd(n),trialRhs(n);Matrix unused(n,n);
            for(unsigned backtrack=0;backtrack<16;++backtrack,alpha*=.5){
                const Vector candidate=state+alpha*step.head(n).cwiseProduct(scale);const double trialPeriod=period+alpha*step[n]*r->period_scale;
                if(trialPeriod<r->min_period||trialPeriod>r->max_period)continue;
                evaluate(candidate,trialPeriod,false,trialEnd,unused,trialRhs);Vector trial(n+1);trial.head(n)=(trialEnd-candidate).cwiseQuotient(scale);trial[n]=phase(candidate);
                if(!trial.allFinite())throw std::runtime_error("Periodic trial closure/phase residual exceeds the finite real range.");
                if(norm(trial)<norm(residual)*(1-1e-4*alpha)||norm(trial)<=r->tolerance){state=candidate;period=trialPeriod;lastStep=alpha*norm(step);lastAlpha=alpha;accepted=true;break;}
            }
            if(!accepted){text(r->status,sizeof(r->status),"line_search_failed");break;}
        }
        std::copy_n(state.data(),n,r->state);std::copy_n(monodromy.data(),n*n,r->monodromy);r->final_period=period;
        if(r->status[0]==0)text(r->status,sizeof(r->status),"inconclusive");
        return 0;
    }catch(const std::exception& e){text(error,capacity,e.what());return host&&host->cancelled&&host->cancelled(host->context)?3:1;}catch(...){text(error,capacity,"Unknown native periodic-orbit failure.");return 1;}
}

int continuation(const penta_orbits_host_v1* host,penta_orbits_continue_v1* r,char* error,uint64_t capacity)noexcept{
    try{
        hostValid(host);
        if(!r||r->struct_size<sizeof(*r)||r->dimension<1||r->dimension>33||!r->initial||!r->residual||!r->states||!r->parameters||!r->tangents||!r->residuals||!r->step_sizes||r->points<2||r->points>1000||r->row_capacity<r->points||r->max_iterations<1||r->max_iterations>100)throw std::runtime_error("Invalid continuation layout/buffers.");
        for(const double x:{r->parameter,r->step,r->min_step,r->max_step,r->min_parameter,r->max_parameter,r->tolerance,r->max_seconds})if(!std::isfinite(x))throw std::runtime_error("Continuation settings must be finite.");
        if(r->step==0||r->min_step<=0||r->max_step<r->min_step||r->min_parameter>=r->max_parameter||r->parameter<r->min_parameter||r->parameter>r->max_parameter||r->tolerance<=0||r->max_seconds<=0)throw std::runtime_error("Invalid continuation parameter/step/tolerance bounds.");
        const auto n=static_cast<Eigen::Index>(r->dimension);Guard guard{host,Clock::now(),r->max_seconds};Vector point(n+1),value(n);point.head(n)=Eigen::Map<const Vector>(r->initial,n);point[n]=r->parameter;Matrix jacobian(n,n+1);r->rows=r->evaluations=r->iterations=0;r->completed=0;
        if(!point.allFinite())throw std::runtime_error("Continuation initial state must be finite.");
        auto evaluate=[&](const Vector& z,Vector& f,Matrix& J){guard.check(static_cast<uint64_t>(n*n));char failure[1024]{};++r->evaluations;if(r->residual(r->context,z.data(),z[n],f.data(),J.data(),failure,sizeof(failure)))throw std::runtime_error(failure[0]?failure:"Native continuation residual failed.");if(!f.allFinite()||!J.allFinite())throw std::runtime_error("Continuation residual/Jacobian is nonfinite.");};
        // Correct the supplied equilibrium at fixed parameter before tracking.
        for(uint64_t k=0;;++k){evaluate(point,value,jacobian);if(norm(value)<=r->tolerance)break;if(k==r->max_iterations){text(r->status,sizeof(r->status),"initial_corrector_failed");return 0;}Vector delta=newtonStep(jacobian.leftCols(n),value);point.head(n)+=delta;++r->iterations;}
        auto tangent=[&](const Matrix& J,const Vector& previous){Eigen::JacobiSVD<Matrix> decomposition(J,Eigen::ComputeFullV);if(decomposition.singularValues().size()!=n||decomposition.singularValues()[n-1]<=1e-12*std::max(1.0,J.norm()))throw std::runtime_error("Continuation augmented Jacobian lost rank.");Vector t=decomposition.matrixV().col(n);if(previous.size()){if(t.dot(previous)<0)t=-t;}else if((r->step>0&&t[n]<0)||(r->step<0&&t[n]>0))t=-t;return t.normalized().eval();};
        Vector direction=tangent(jacobian,Vector{});double stepSize=std::clamp(std::abs(r->step),r->min_step,r->max_step);
        auto save=[&](double step){const auto row=r->rows;std::copy_n(point.data(),n,r->states+row*static_cast<uint64_t>(n));r->parameters[row]=point[n];std::copy_n(direction.data(),n+1,r->tangents+row*static_cast<uint64_t>(n+1));r->residuals[row]=norm(value);r->step_sizes[row]=step;++r->rows;};save(0);
        while(r->rows<r->points){
            bool accepted=false;Vector next(n+1),nextValue(n);Matrix nextJacobian(n,n+1);
            while(stepSize>=r->min_step){
                const Vector predictor=point+stepSize*direction;next=predictor;
                if(next[n]<r->min_parameter||next[n]>r->max_parameter){text(r->status,sizeof(r->status),"parameter_limit");return 0;}
                bool failed=false;
                for(uint64_t k=0;k<=r->max_iterations;++k){
                    evaluate(next,nextValue,nextJacobian);Vector residual(n+1);residual.head(n)=nextValue;residual[n]=direction.dot(next-predictor);
                    if(norm(residual)<=r->tolerance){accepted=true;break;}if(k==r->max_iterations){failed=true;break;}
                    Matrix augmented(n+1,n+1);augmented.topRows(n)=nextJacobian;augmented.bottomRows(1)=direction.transpose();Vector delta;
                    try{delta=newtonStep(augmented,residual);}catch(...){failed=true;break;}
                    bool moved=false;double alpha=1;
                    for(unsigned line=0;line<12;++line,alpha*=.5){const Vector candidate=next+alpha*delta;if(candidate[n]<r->min_parameter||candidate[n]>r->max_parameter)continue;Vector candidateValue(n);Matrix candidateJacobian(n,n+1);evaluate(candidate,candidateValue,candidateJacobian);Vector trial(n+1);trial.head(n)=candidateValue;trial[n]=direction.dot(candidate-predictor);if(norm(trial)<norm(residual)||norm(trial)<=r->tolerance){next=candidate;nextValue=candidateValue;nextJacobian=candidateJacobian;moved=true;break;}}
                    ++r->iterations;if(!moved){failed=true;break;}
                }
                if(accepted&&!failed)break;
                stepSize*=.5;
            }
            if(!accepted){text(r->status,sizeof(r->status),"minimum_step_reached");return 0;}
            point=next;value=nextValue;jacobian=nextJacobian;direction=tangent(jacobian,direction);save(stepSize);stepSize=std::min(r->max_step,stepSize*1.2);
        }
        r->completed=1;text(r->status,sizeof(r->status),"completed");return 0;
    }catch(const std::exception& e){text(error,capacity,e.what());return host&&host->cancelled&&host->cancelled(host->context)?3:1;}catch(...){text(error,capacity,"Unknown native continuation failure.");return 1;}
}
int spectrum(penta_orbits_spectrum_v1* r,char* error,uint64_t capacity)noexcept{
    try{
        if(!r||r->struct_size<sizeof(*r)||r->dimension<1||r->dimension>64||!r->matrix||!r->real_parts||!r->imaginary_parts)throw std::runtime_error("Invalid spectrum request.");
        const auto n=static_cast<Eigen::Index>(r->dimension);const Matrix matrix=Eigen::Map<const Matrix>(r->matrix,n,n);if(!matrix.allFinite())throw std::runtime_error("Spectrum input is nonfinite.");Eigen::EigenSolver<Matrix> eig(matrix,false);if(eig.info()!=Eigen::Success)throw std::runtime_error("Native spectrum calculation failed.");
        std::vector<Eigen::Index> order(static_cast<std::size_t>(n));std::iota(order.begin(),order.end(),0);const auto values=eig.eigenvalues();std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){if(std::abs(values[a])!=std::abs(values[b]))return std::abs(values[a])>std::abs(values[b]);if(values[a].real()!=values[b].real())return values[a].real()>values[b].real();return values[a].imag()>values[b].imag();});
        for(Eigen::Index k=0;k<n;++k){r->real_parts[k]=values[order[static_cast<std::size_t>(k)]].real();r->imaginary_parts[k]=values[order[static_cast<std::size_t>(k)]].imag();}return 0;
    }catch(const std::exception& e){text(error,capacity,e.what());return 1;}catch(...){text(error,capacity,"Unknown native spectral failure.");return 1;}
}
const penta_orbits_api_v1 api{sizeof(penta_orbits_api_v1),PENTA_ORBITS_ABI_V1,7,periodic,continuation,spectrum};
}
#ifdef _WIN32
#define PENTA_ORBITS_EXPORT __declspec(dllexport)
#else
#define PENTA_ORBITS_EXPORT __attribute__((visibility("default")))
#endif
extern "C" PENTA_ORBITS_EXPORT const penta_orbits_api_v1* penta_orbits_query_v1(uint32_t version){return version==PENTA_ORBITS_ABI_V1?&api:nullptr;}
