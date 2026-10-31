#define PENTA_FIT_MODULE_BUILD
#define EIGEN_DONT_PARALLELIZE
#include "fit_api.h"
#include <ceres/cost_function.h>
#include <ceres/iteration_callback.h>
#include <ceres/loss_function.h>
#include <ceres/manifold.h>
#include <ceres/problem.h>
#include <ceres/solver.h>
#include <Eigen/SVD>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace {
using Clock=std::chrono::steady_clock;
void text(char* out,uint64_t capacity,const std::string& value){if(out&&capacity){const auto n=std::min<uint64_t>(capacity-1,value.size());std::memcpy(out,value.data(),static_cast<std::size_t>(n));out[n]=0;}}
struct Error:std::runtime_error{int status;Error(int code,const std::string& message):std::runtime_error(message),status(code){}};
double robust(double squared,unsigned loss,double scale){const double a=scale*scale;if(loss==1)return squared<=a?squared:2*scale*std::sqrt(squared)-a;if(loss==2)return a*std::log1p(squared/a);if(loss==3)return 2*a*(std::sqrt(1+squared/a)-1);return squared;}
double influence(double squared,unsigned loss,double scale){const double a=scale*scale;if(loss==1)return squared<=a?1:scale/std::sqrt(squared);if(loss==2)return 1/(1+squared/a);if(loss==3)return 1/std::sqrt(1+squared/a);return 1;}
struct Context {
    const penta_fit_host_v1& host;const penta_fit_request_v1& request;penta_fit_result_v1& report;
    Clock::time_point start=Clock::now();std::mutex mutex;int failure=0;std::string error;
    std::vector<double> scale,weights,currentZ,currentX,residual,jacobian,bestZ,bestX,bestResidual,bestJacobian;
    std::set<std::thread::id> workers;
    bool cached=false,hasJacobian=false,bestHasJacobian=false,userStop=false,initialized=false;double bestCost=std::numeric_limits<double>::infinity();uint64_t run=1;
    const std::vector<double>* acceptedZ=nullptr;
    Context(const penta_fit_host_v1& h,const penta_fit_request_v1& q,penta_fit_result_v1& r):host(h),request(q),report(r){}
    double elapsed()const{return std::chrono::duration<double>(Clock::now()-start).count();}
    bool notify(uint32_t phase,const std::vector<double>& x,double cost,double step,uint32_t flags){
        if(host.struct_size<sizeof(penta_fit_host_v1)||!host.output)return true;
        penta_iteration_snapshot_v1 snapshot{sizeof(snapshot),PENTA_ITERATION_ABI_V1,phase,PENTA_ITERATION_EVENT_ITERATION,flags,0,report.iterations,report.evaluations,run,x.size(),x.data(),cost,(flags&PENTA_ITERATION_HAS_RESIDUAL_NORM)?report.residual_norm:0,step,elapsed()};char message[2048]{};const int action=host.output(host.context,&snapshot,message,sizeof(message));
        if(action<0){failure=PENTA_FIT_CALLBACK;error=message[0]?message:"OutputFcn failed.";return false;}if(action>0&&phase!=PENTA_ITERATION_DONE)userStop=true;return true;
    }
    bool check(){if(failure)return false;if(host.cancelled&&host.cancelled(host.context)){failure=PENTA_FIT_CANCELLED;error="Native fitting cancelled.";return false;}if(elapsed()>=request.maximum_wall_seconds){failure=PENTA_FIT_BUDGET;error="Fitting wall-time budget reached.";return false;}return true;}
    bool evaluate(const double* z,bool derivatives){
        if(!check())return false;
        const auto n=static_cast<std::size_t>(request.dimension),m=static_cast<std::size_t>(request.residual_count);
        if(cached&&std::equal(currentZ.begin(),currentZ.end(),z)&&(!derivatives||hasJacobian))return true;
        if(report.evaluations>=request.max_evaluations){failure=PENTA_FIT_BUDGET;error="Fitting native callback evaluation budget reached.";return false;}
        currentZ.assign(z,z+n);for(std::size_t c=0;c<n;++c){currentX[c]=z[c]*scale[c];if(!std::isfinite(currentX[c])){failure=PENTA_FIT_DOMAIN;error="Scaled fitting parameters overflowed.";return false;}}
        char message[2048]{};
        if(host.charge&&host.charge(host.context,static_cast<uint64_t>(m)*(derivatives?n:1),message,sizeof(message))){failure=PENTA_FIT_BUDGET;error=message;return false;}
        ++report.evaluations;if(derivatives)++report.jacobian_evaluations;
        const int status=request.evaluate(request.callback_context,currentX.data(),n,residual.data(),m,derivatives?jacobian.data():nullptr,message,sizeof(message));
        if(status){failure=PENTA_FIT_CALLBACK;error=message[0]?message:"Native residual callback failed.";return false;}
        double cost=0;for(std::size_t r=0;r<m;++r){const double weighted=std::sqrt(weights[r])*residual[r];if(!std::isfinite(weighted)){failure=PENTA_FIT_DOMAIN;error="A native fitting residual is nonfinite.";return false;}cost+=.5*robust(weighted*weighted,request.loss,request.loss_scale);}
        if(!std::isfinite(cost)){failure=PENTA_FIT_DOMAIN;error="Fitting objective exceeds the finite real range.";return false;}
        if(derivatives&&std::any_of(jacobian.begin(),jacobian.end(),[](double v){return !std::isfinite(v);})){failure=PENTA_FIT_DOMAIN;error="Native residual Jacobian is nonfinite.";return false;}
        cached=true;hasJacobian=derivatives;
        if(cost<bestCost||(currentX==bestX&&derivatives&&!bestHasJacobian)){bestCost=cost;bestZ=currentZ;bestX=currentX;bestResidual=residual;bestHasJacobian=derivatives;bestJacobian=derivatives?jacobian:std::vector<double>{};report.best_start=run;}
        return true;
    }
};
class Residual final:public ceres::CostFunction {
    Context& context_;std::size_t row_;
public:
    Residual(Context& context,std::size_t row):context_(context),row_(row){set_num_residuals(1);mutable_parameter_block_sizes()->push_back(static_cast<int>(context.request.dimension));}
    bool Evaluate(double const* const* parameters,double* residuals,double** jacobians)const override{
        std::lock_guard lock(context_.mutex);context_.workers.insert(std::this_thread::get_id());const bool derivative=jacobians&&jacobians[0];
        if(!context_.evaluate(parameters[0],derivative))return false;
        const auto n=static_cast<std::size_t>(context_.request.dimension);const auto weight=std::sqrt(context_.weights[row_]);residuals[0]=weight*context_.residual[row_];
        if(derivative)for(std::size_t c=0;c<n;++c)jacobians[0][c]=weight*context_.jacobian[row_*n+c]*context_.scale[c];
        return true;
    }
};
class Iteration final:public ceres::IterationCallback {
    Context& context_;
public:explicit Iteration(Context& context):context_(context){}
    ceres::CallbackReturnType operator()(const ceres::IterationSummary& summary)override{
        auto& report=context_.report;if(summary.iteration>0)++report.iterations;
        if(!std::isfinite(summary.cost)||!std::isfinite(summary.gradient_max_norm)||!std::isfinite(summary.step_norm)){context_.failure=PENTA_FIT_DOMAIN;context_.error="A Ceres iteration diagnostic exceeds the finite real range.";return ceres::SOLVER_ABORT;}
        if(report.history_rows<context_.request.history_capacity){auto* out=report.history+5*report.history_rows++;out[0]=static_cast<double>(context_.run);out[1]=summary.iteration;out[2]=summary.cost;out[3]=summary.gradient_max_norm;out[4]=summary.step_norm;}
        if(!context_.check())return ceres::SOLVER_ABORT;
        if(summary.iteration>0&&context_.acceptedZ){std::vector<double> accepted(context_.acceptedZ->size());for(std::size_t j=0;j<accepted.size();++j)accepted[j]=(*context_.acceptedZ)[j]*context_.scale[j];const uint32_t flags=PENTA_ITERATION_HAS_OBJECTIVE|PENTA_ITERATION_HAS_STEP_NORM|(summary.step_is_successful?PENTA_ITERATION_ACCEPTED:0);if(!context_.notify(PENTA_ITERATION_ITER,accepted,summary.cost,summary.step_norm,flags))return ceres::SOLVER_ABORT;if(context_.userStop)return ceres::SOLVER_TERMINATE_SUCCESSFULLY;}
        if(report.iterations>=context_.request.max_iterations){context_.failure=PENTA_FIT_BUDGET;context_.error="Fitting iteration budget reached.";return ceres::SOLVER_ABORT;}return ceres::SOLVER_CONTINUE;
    }
};
int solve(const penta_fit_host_v1* host,const penta_fit_request_v1* request,penta_fit_result_v1* output,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||!request||!output||host->struct_size<offsetof(penta_fit_host_v1,output)||host->abi_version!=PENTA_FIT_ABI_V1||request->struct_size<sizeof(*request)||output->struct_size<sizeof(*output))throw Error(PENTA_FIT_VERSION,"Fitting ABI/structure mismatch.");
        const auto& q=*request;auto& r=*output;const auto n=static_cast<std::size_t>(q.dimension),m=static_cast<std::size_t>(q.residual_count);
        if(!q.evaluate||!q.initial||n<1||n>128||m<1||m>65536||n*m>1048576||q.start_count>256||q.max_iterations<1||q.max_iterations>1000000||q.max_evaluations<1||q.max_evaluations>10000000||q.history_capacity>100000||q.threads<1||q.threads>64||q.method<1||q.method>2||q.loss>3||q.covariance>1)throw Error(PENTA_FIT_ARGUMENT,"Invalid fitting dimensions, algorithm or budgets.");
        if(q.start_count&&!q.starts)throw Error(PENTA_FIT_ARGUMENT,"Multistart count requires start rows.");
        if(!r.solution||r.solution_capacity<n||!r.residuals||r.residual_capacity<m||!r.jacobian||r.jacobian_capacity<n*m||!r.covariance_matrix||r.covariance_capacity<n*n||(q.history_capacity&&(!r.history||r.history_capacity<5*q.history_capacity)))throw Error(PENTA_FIT_ARGUMENT,"Insufficient host fitting result storage.");
        for(const double v:{q.loss_scale,q.function_tolerance,q.gradient_tolerance,q.parameter_tolerance,q.maximum_wall_seconds})if(!std::isfinite(v)||v<=0)throw Error(PENTA_FIT_ARGUMENT,"Fitting tolerances, robust scale and wall budget must be finite positive values.");
        if(!std::isfinite(q.noise_variance)||(q.noise_variance<0&&q.noise_variance!=-1))throw Error(PENTA_FIT_ARGUMENT,"noise_variance must be nonnegative or -1 for a local residual estimate.");
        Context context{*host,q,r};context.currentX.resize(n);context.residual.resize(m);context.jacobian.resize(m*n);context.scale.assign(n,1);context.weights.assign(m,1);if(q.parameter_scale)context.scale.assign(q.parameter_scale,q.parameter_scale+n);if(q.weights)context.weights.assign(q.weights,q.weights+m);
        for(double v:context.scale)if(!std::isfinite(v)||v<=0)throw Error(PENTA_FIT_ARGUMENT,"parameter_scale requires finite positive entries.");
        uint64_t positiveWeights=0;for(double v:context.weights){if(!std::isfinite(v)||v<0)throw Error(PENTA_FIT_ARGUMENT,"weights require finite nonnegative entries.");if(v>0)++positiveWeights;}if(!positiveWeights)throw Error(PENTA_FIT_ARGUMENT,"At least one residual weight must be positive.");
        std::vector<double> lower(n,-std::numeric_limits<double>::infinity()),upper(n,std::numeric_limits<double>::infinity());if(q.lower)std::copy_n(q.lower,n,lower.begin());if(q.upper)std::copy_n(q.upper,n,upper.begin());
        for(std::size_t c=0;c<n;++c)if(std::isnan(lower[c])||std::isnan(upper[c])||lower[c]>=upper[c])throw Error(PENTA_FIT_ARGUMENT,"Every fitting lower bound must be strictly below its upper bound.");
        const auto count=q.start_count?q.start_count:1;std::vector<double> z(n);bool bestConverged=false;int bestFlag=ceres::NO_CONVERGENCE;std::string bestMessage;
        r.threads=q.threads;r.threads_used=1;r.initial_cost=0;r.cost=0;r.history_rows=0;r.iterations=0;r.evaluations=0;r.jacobian_evaluations=0;r.starts_completed=0;r.best_start=0;r.rank=0;r.covariance_valid=0;r.converged=0;
        for(uint64_t start=0;start<count;++start){
            context.run=start+1;const double* initial=q.start_count?q.starts+start*n:q.initial;for(std::size_t c=0;c<n;++c){if(!std::isfinite(initial[c])||initial[c]<lower[c]||initial[c]>upper[c])throw Error(PENTA_FIT_ARGUMENT,"Every fitting start must lie inside its bounds.");z[c]=initial[c]/context.scale[c];if(!std::isfinite(z[c]))throw Error(PENTA_FIT_ARGUMENT,"Scaled fitting start overflowed.");}
            context.cached=false;if(!context.evaluate(z.data(),true))break;if(start==0)r.initial_cost=context.bestCost;
            context.acceptedZ=&z;if(!context.initialized){context.initialized=true;if(!context.notify(PENTA_ITERATION_INIT,context.currentX,context.bestCost,0,PENTA_ITERATION_HAS_OBJECTIVE|PENTA_ITERATION_ACCEPTED)||context.userStop)break;}
            std::vector<int> frozen;
            for(std::size_t refinement=0;refinement<=n&&!context.failure&&!context.userStop;++refinement){
            ceres::Problem problem;for(std::size_t row=0;row<m;++row){ceres::LossFunction* loss=nullptr;if(q.loss==1)loss=new ceres::HuberLoss(q.loss_scale);else if(q.loss==2)loss=new ceres::CauchyLoss(q.loss_scale);else if(q.loss==3)loss=new ceres::SoftLOneLoss(q.loss_scale);problem.AddResidualBlock(new Residual(context,row),loss,z.data());}
            for(std::size_t c=0;c<n;++c){if(std::isfinite(lower[c]))problem.SetParameterLowerBound(z.data(),static_cast<int>(c),lower[c]/context.scale[c]);if(std::isfinite(upper[c]))problem.SetParameterUpperBound(z.data(),static_cast<int>(c),upper[c]/context.scale[c]);}
            if(!frozen.empty()){if(frozen.size()==n)problem.SetParameterBlockConstant(z.data());else problem.SetManifold(z.data(),new ceres::SubsetManifold(static_cast<int>(n),frozen));}
            ceres::Solver::Options options;options.trust_region_strategy_type=q.method==1?ceres::LEVENBERG_MARQUARDT:ceres::DOGLEG;options.dogleg_type=ceres::TRADITIONAL_DOGLEG;options.linear_solver_type=ceres::DENSE_QR;options.num_threads=static_cast<int>(q.threads);options.max_num_iterations=static_cast<int>(q.max_iterations-r.iterations);options.max_solver_time_in_seconds=std::max(1e-9,q.maximum_wall_seconds-context.elapsed());options.function_tolerance=q.function_tolerance;options.gradient_tolerance=q.gradient_tolerance;options.parameter_tolerance=q.parameter_tolerance;options.logging_type=ceres::SILENT;options.minimizer_progress_to_stdout=false;options.update_state_every_iteration=true;Iteration iteration(context);options.callbacks.push_back(&iteration);
            context.workers.clear();ceres::Solver::Summary summary;ceres::Solve(options,&problem,&summary);r.threads_used=std::max<uint64_t>(r.threads_used,context.workers.size());
            if(!context.failure&&!context.userStop)context.evaluate(z.data(),true);
            if(r.best_start==start+1){bestConverged=summary.termination_type==ceres::CONVERGENCE||summary.termination_type==ceres::USER_SUCCESS;bestFlag=summary.termination_type;bestMessage=summary.BriefReport();}
            if(context.failure||context.userStop)break;
            // Ceres projects bounded trust-region steps. On a binding face that
            // projection can stall while free coordinates still have a gradient.
            // Refine that face with Ceres' exact subset manifold; recompute the
            // active set after each solve, with the same global work/time limits.
            std::vector<double> gradient(n,0);for(std::size_t row=0;row<m;++row){const double w=context.weights[row],value=context.residual[row],factor=w*value*influence(w*value*value,q.loss,q.loss_scale);for(std::size_t c=0;c<n;++c)gradient[c]+=factor*context.jacobian[row*n+c];}
            std::vector<int> active;double projected=0;for(std::size_t c=0;c<n;++c){const double x=context.currentX[c],tolerance=100*std::numeric_limits<double>::epsilon()*(1+std::abs(x));const bool blocked=(std::isfinite(lower[c])&&x-lower[c]<=tolerance&&gradient[c]>0)||(std::isfinite(upper[c])&&upper[c]-x<=tolerance&&gradient[c]<0);if(blocked)active.push_back(static_cast<int>(c));else projected=std::max(projected,std::abs(gradient[c]*context.scale[c]));}
            if(active==frozen||projected<=std::max(q.gradient_tolerance,1e-12))break;
            if(refinement==n){if(r.best_start==start+1){bestConverged=false;bestMessage+="; active-set refinement limit reached";}break;}
            frozen=std::move(active);
            }
            ++r.starts_completed;
            if(context.failure||context.userStop)break;
        }
        if(!context.failure&&!context.userStop&&!context.bestHasJacobian&&!context.bestZ.empty()){const auto winning=context.bestZ;context.run=r.best_start;context.cached=false;context.evaluate(winning.data(),true);}
        if(context.failure&&context.failure!=PENTA_FIT_BUDGET)throw Error(context.failure,context.error);
        if(context.bestX.empty())throw Error(context.failure?context.failure:PENTA_FIT_INTERNAL,context.error.empty()?"No finite fitting candidate was evaluated.":context.error);
        r.converged=!context.failure&&!context.userStop&&bestConverged;r.backend_flag=bestFlag;r.cost=context.bestCost;r.elapsed_seconds=context.elapsed();
        text(r.status,sizeof(r.status),context.userStop?"output_function_stop":context.failure?"budget_reached":r.converged?"converged":"not_converged");text(r.message,sizeof(r.message),context.userStop?"Stopped cleanly by OutputFcn.":context.failure?context.error:bestMessage);
        std::copy(context.bestX.begin(),context.bestX.end(),r.solution);std::copy(context.bestResidual.begin(),context.bestResidual.end(),r.residuals);std::fill_n(r.jacobian,n*m,0);std::fill_n(r.covariance_matrix,n*n,0);
        r.residual_norm=0;r.weighted_residual_norm=0;for(std::size_t row=0;row<m;++row){r.residual_norm=std::hypot(r.residual_norm,context.bestResidual[row]);r.weighted_residual_norm=std::hypot(r.weighted_residual_norm,std::sqrt(context.weights[row])*context.bestResidual[row]);}
        r.rcond=0;r.gradient_norm=0;r.noise_variance=q.noise_variance;r.jacobian_available=context.bestHasJacobian;bool activeBound=false;
        for(std::size_t c=0;c<n;++c){const double tolerance=100*std::numeric_limits<double>::epsilon()*(1+std::abs(context.bestX[c]));activeBound|=(std::isfinite(lower[c])&&context.bestX[c]-lower[c]<=tolerance)||(std::isfinite(upper[c])&&upper[c]-context.bestX[c]<=tolerance);}
        if(context.bestHasJacobian){
            std::copy(context.bestJacobian.begin(),context.bestJacobian.end(),r.jacobian);Eigen::MatrixXd weighted(static_cast<Eigen::Index>(m),static_cast<Eigen::Index>(n));Eigen::VectorXd gradient=Eigen::VectorXd::Zero(static_cast<Eigen::Index>(n));
            for(std::size_t row=0;row<m;++row){const double w=context.weights[row],value=context.bestResidual[row],factor=w*value*influence(w*value*value,q.loss,q.loss_scale);for(std::size_t c=0;c<n;++c){weighted(static_cast<Eigen::Index>(row),static_cast<Eigen::Index>(c))=std::sqrt(w)*context.bestJacobian[row*n+c];gradient[static_cast<Eigen::Index>(c)]+=factor*context.bestJacobian[row*n+c];}}
            for(std::size_t c=0;c<n;++c){const double x=context.bestX[c],tolerance=100*std::numeric_limits<double>::epsilon()*(1+std::abs(x));if((std::isfinite(lower[c])&&x-lower[c]<=tolerance&&gradient[static_cast<Eigen::Index>(c)]>0)||(std::isfinite(upper[c])&&upper[c]-x<=tolerance&&gradient[static_cast<Eigen::Index>(c)]<0))gradient[static_cast<Eigen::Index>(c)]=0;}
            r.gradient_norm=gradient.lpNorm<Eigen::Infinity>();Eigen::JacobiSVD<Eigen::MatrixXd> svd(weighted,Eigen::ComputeThinV);const auto singular=svd.singularValues();const double largest=singular.size()?singular[0]:0,threshold=std::numeric_limits<double>::epsilon()*static_cast<double>(std::max(m,n))*largest;
            for(Eigen::Index index=0;index<singular.size();++index)if(singular[index]>threshold)++r.rank;
            if(r.rank==n&&largest>0)r.rcond=singular[static_cast<Eigen::Index>(n-1)]/largest;
            if(q.covariance&&q.loss==0&&!activeBound&&r.rank==n&&(q.noise_variance>=0||positiveWeights>n)&&r.converged){
                const double sigma=q.noise_variance<0?r.weighted_residual_norm/std::sqrt(static_cast<double>(positiveWeights-n)):std::sqrt(q.noise_variance);const double variance=sigma*sigma;
                Eigen::MatrixXd scaled=svd.matrixV();for(Eigen::Index column=0;column<singular.size();++column)scaled.col(column)*=sigma/singular[column];const Eigen::MatrixXd covariance=scaled*scaled.transpose();
                if(covariance.allFinite()&&std::isfinite(variance)){for(std::size_t row=0;row<n;++row)for(std::size_t c=0;c<n;++c)r.covariance_matrix[row*n+c]=covariance(static_cast<Eigen::Index>(row),static_cast<Eigen::Index>(c));r.covariance_valid=1;r.noise_variance=variance;}
            }
        }
        text(r.covariance_assumption,sizeof(r.covariance_assumption),r.covariance_valid?(q.noise_variance<0?"Local linearization; independent Gaussian residuals after sqrt(weights) scaling; weighted residual variance estimated with m_positive-rank degrees of freedom.":"Local linearization; independent Gaussian residuals after sqrt(weights) scaling; supplied noise_variance. No active bounds; full column rank."):"Unavailable unless requested, converged, linear loss, inactive bounds and finite full-rank Jacobian; estimating noise also requires positive residual degrees of freedom. Local rank does not establish parameter uniqueness.");
        if(!std::isfinite(r.residual_norm)||!std::isfinite(r.weighted_residual_norm)||!std::isfinite(r.gradient_norm))throw Error(PENTA_FIT_DOMAIN,"Fitting diagnostic exceeds the finite real range.");
        if(!context.notify(PENTA_ITERATION_DONE,context.bestX,r.cost,0,PENTA_ITERATION_HAS_OBJECTIVE|PENTA_ITERATION_HAS_RESIDUAL_NORM|PENTA_ITERATION_ACCEPTED))throw Error(context.failure,context.error);
        return PENTA_FIT_OK;
    }catch(const Error& value){text(error,capacity,value.what());return value.status;}catch(const std::exception& value){text(error,capacity,value.what());return PENTA_FIT_INTERNAL;}catch(...){text(error,capacity,"Unknown native fitting failure.");return PENTA_FIT_INTERNAL;}
}
const penta_fit_api_v1 api{sizeof(api),PENTA_FIT_ABI_V1,"penta_fit","ceres-eigen-fit-v1-output",1|PENTA_ITERATION_CAPABILITY_V1,solve};
}
extern "C" PENTA_FIT_EXPORT const penta_fit_api_v1* penta_fit_query_v1(uint32_t version){return version==PENTA_FIT_ABI_V1?&api:nullptr;}
