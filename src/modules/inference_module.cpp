#include "inference_api.h"
#include "inference/noise_models.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void message(char* out,uint64_t cap,const char* text){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
uint64_t mix(uint64_t n){n+=UINT64_C(0x9e3779b97f4a7c15);n=(n^(n>>30))*UINT64_C(0xbf58476d1ce4e5b9);n=(n^(n>>27))*UINT64_C(0x94d049bb133111eb);return n^(n>>31);}
uint64_t index(std::mt19937_64& random,uint64_t count){const auto limit=uint64_t(-count)%count;uint64_t n;do{n=random();}while(n<limit);return n%count;}
struct CleanStop{};
struct Experiment;
struct Curve {
    Experiment& experiment;std::vector<double> x,y,full;std::vector<unsigned> free;
    int evaluate(const double*,uint64_t,double*,uint64_t,double*,char*,uint64_t);
    static int callback(void* raw,const double* point,uint64_t n,double* residual,uint64_t m,double* jac,char* error,uint64_t capacity)noexcept{try{return static_cast<Curve*>(raw)->evaluate(point,n,residual,m,jac,error,capacity);}catch(const std::exception& e){message(error,capacity,e.what());return PENTA_FIT_CALLBACK;}catch(...){message(error,capacity,"Unknown compiled refit model failure.");return PENTA_FIT_CALLBACK;}}
};
struct Experiment {
    const penta_inference_host_v1& host;const penta_inference_request_v1& request;penta_inference_result_v1& result;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    void check()const{if(host.stopped&&host.stopped(host.context))throw CleanStop{};if(host.cancelled&&host.cancelled(host.context))throw std::runtime_error("Inference experiment cancelled.");if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>request.max_wall_seconds)throw std::runtime_error("Inference experiment wall-time budget exceeded.");}
    double prediction(const double* point,double x,double* jac=nullptr)const{check();char error[2048]{};double value=0;if(request.model(request.model_context,point,request.dimension,x,&value,jac,error,sizeof(error)))throw std::runtime_error(error[0]?error:"Compiled inference model failed.");if(!std::isfinite(value))throw std::runtime_error("Inference model returned a non-finite prediction.");if(jac)for(unsigned j=0;j<request.dimension;++j)if(!std::isfinite(jac[j]))throw std::runtime_error("Inference model returned a non-finite Jacobian.");return value;}
    struct Fit {std::vector<double> parameters;double cost=0;uint64_t iterations=0,evaluations=0,rank=0;bool converged=false;std::string message;};
    Fit fit(Curve& curve,const std::vector<double>& initial,uint64_t taskId=0)const{
        check();const auto n=curve.free.size(),m=curve.x.size();Fit fit;fit.parameters=curve.full;
        if(!n){long double sum=0;for(std::size_t i=0;i<m;++i){const auto delta=penta::inference::noise::residual(static_cast<penta::inference::noise::Model>(request.fit.reserved),prediction(curve.full.data(),curve.x[i]),curve.y[i],request.fit.noise_variance,request.fit.loss_scale).value;sum+=static_cast<long double>(delta)*delta;}fit.cost=static_cast<double>(.5*sum/(request.fit.reserved==0&&request.fit.noise_variance>0?request.fit.noise_variance:1));if(!std::isfinite(fit.cost))throw std::runtime_error("Profile objective overflowed.");fit.evaluations=1;fit.converged=true;fit.message="No nuisance coordinates; direct profile evaluation.";return fit;}
        penta_fit_request_v1 q=request.fit;q.struct_size=sizeof(q);q.dimension=n;q.residual_count=m;q.initial=initial.data();q.starts=nullptr;q.start_count=0;q.threads=1;q.loss=0;q.covariance=0;q.history_capacity=0;q.evaluate=Curve::callback;q.callback_context=&curve;
        const auto remaining=request.max_wall_seconds-std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();q.maximum_wall_seconds=std::min(q.maximum_wall_seconds,remaining);if(q.maximum_wall_seconds<=0)throw std::runtime_error("Inference experiment wall-time budget exceeded.");
        std::vector<double> lower(n),upper(n),scale(n,1),weights(m,request.fit.reserved==0&&request.fit.noise_variance>0?1/request.fit.noise_variance:1);
        for(std::size_t i=0;i<n;++i){const auto j=curve.free[i];lower[i]=request.fit.lower?request.fit.lower[j]:-std::numeric_limits<double>::infinity();upper[i]=request.fit.upper?request.fit.upper[j]:std::numeric_limits<double>::infinity();if(request.fit.parameter_scale)scale[i]=request.fit.parameter_scale[j];}q.lower=lower.data();q.upper=upper.data();q.parameter_scale=scale.data();q.weights=weights.data();
        std::vector<double> solution(n),residual(m),jacobian(n*m),covariance(n*n);penta_fit_result_v1 output{};output.struct_size=sizeof(output);output.solution=solution.data();output.solution_capacity=n;output.residuals=residual.data();output.residual_capacity=m;output.jacobian=jacobian.data();output.jacobian_capacity=n*m;output.covariance_matrix=covariance.data();output.covariance_capacity=n*n;char error[2048]{};
        const penta_inference_fit_metadata_v1 metadata{sizeof(metadata),request.dimension,taskId,n,curve.free.data(),curve.full.data()};
        const auto status=host.fit(host.context,&q,&metadata,&output,error,sizeof(error));
        if((host.stopped&&host.stopped(host.context))||status==PENTA_INFERENCE_STOPPED||std::strcmp(output.status,"output_function_stop")==0)throw CleanStop{};
        if(status)throw std::runtime_error(error[0]?error:"Native nuisance refit failed.");
        fit.cost=output.cost;fit.converged=output.converged;fit.iterations=output.iterations;fit.evaluations=output.evaluations;fit.rank=output.rank;fit.message=output.message;for(std::size_t i=0;i<n;++i)fit.parameters[curve.free[i]]=solution[i];return fit;
    }
    Curve ordinary(const double* initial)const{Curve curve{const_cast<Experiment&>(*this),std::vector<double>(request.x,request.x+request.observations),std::vector<double>(request.y,request.y+request.observations),std::vector<double>(initial,initial+request.dimension),{}};curve.free.resize(request.dimension);std::iota(curve.free.begin(),curve.free.end(),0);return curve;}
    void task(uint64_t row){
        if(result.status[row]!=PENTA_INF_PENDING)return;
        check();const uint64_t n=request.dimension,k=request.prediction_count;
        try{
            Curve curve=ordinary((request.operation==PENTA_INF_MULTISTART||request.operation==PENTA_INF_COVERAGE)?request.starts+row*n:result.base_parameters);std::vector<double> initial;
            if(request.operation==PENTA_INF_PROFILE){curve.full[request.profile_coordinate]=request.profile_grid[row];curve.free.erase(curve.free.begin()+request.profile_coordinate);}
            else if(request.operation==PENTA_INF_COVERAGE){std::mt19937_64 random(mix(request.seed^mix(row)));for(uint64_t i=0;i<request.observations;++i){check();curve.y[i]=penta::inference::noise::sample(static_cast<penta::inference::noise::Model>(request.fit.reserved),result.base_predictions[i],request.fit.noise_variance,request.fit.loss_scale,random);}}
            else if(request.operation==PENTA_INF_BOOTSTRAP){std::mt19937_64 random(mix(request.seed^mix(row)));if(request.bootstrap_method==0){for(uint64_t i=0;i<request.observations;++i){const auto source=index(random,request.observations);curve.x[i]=request.x[source];curve.y[i]=request.y[source];}}
                else if(request.bootstrap_method==3){for(uint64_t i=0;i<request.observations;++i)curve.y[i]=penta::inference::noise::sample(static_cast<penta::inference::noise::Model>(request.fit.reserved),result.base_predictions[i],request.fit.noise_variance,request.fit.loss_scale,random);}
                else{
                    double mean=0;std::vector<double> residual(request.observations);for(uint64_t i=0;i<request.observations;++i){residual[i]=request.fit.reserved==1?std::log(request.y[i])-std::log(result.base_predictions[i]):request.y[i]-result.base_predictions[i];mean+=residual[i]/request.observations;}uint64_t blockStart=0;
                    for(uint64_t i=0;i<request.observations;++i){if(request.bootstrap_method==1||i%request.block_length==0)blockStart=index(random,request.observations);const auto source=request.bootstrap_method==1?blockStart:(blockStart+i%request.block_length)%request.observations;curve.y[i]=request.fit.reserved==1?std::exp(std::log(result.base_predictions[i])+residual[source]-mean):result.base_predictions[i]+residual[source]-mean;}
                }
            }
            for(const auto j:curve.free)initial.push_back(curve.full[j]);
            const auto fitResult=fit(curve,initial,row+1);std::copy(fitResult.parameters.begin(),fitResult.parameters.end(),result.parameters+row*n);result.costs[row]=fitResult.cost;result.iterations[row]=fitResult.iterations;result.evaluations[row]=fitResult.evaluations;result.ranks[row]=fitResult.rank;for(uint64_t i=0;i<k;++i)result.predictions[row*k+i]=prediction(fitResult.parameters.data(),request.prediction_x[i]);const auto fitMessage=fitResult.message+(fitResult.rank<curve.free.size()?" Numerical convergence with rank-deficient nuisance Jacobian; parameters need not be unique.":"");message(result.messages+row*512,512,fitMessage.c_str());result.status[row]=fitResult.converged?(fitResult.rank<curve.free.size()?PENTA_INF_RANK_DEFICIENT:PENTA_INF_CONVERGED):PENTA_INF_NOT_CONVERGED;
        }catch(const CleanStop&){throw;}catch(const std::exception& e){result.status[row]=PENTA_INF_FAILED;message(result.messages+row*512,512,e.what());}
    }
    static int tasks(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Experiment*>(raw);try{for(uint64_t row=begin;row<end;++row){if(self.host.cancelled&&self.host.cancelled(self.host.context))return PENTA_NUMERIC_CANCELLED;if(self.host.stopped&&self.host.stopped(self.host.context))return PENTA_INFERENCE_STOPPED;self.task(row);}return 0;}catch(const CleanStop&){return PENTA_INFERENCE_STOPPED;}catch(const std::exception& e){message(error,capacity,e.what());return self.host.cancelled&&self.host.cancelled(self.host.context)?PENTA_NUMERIC_CANCELLED:PENTA_NUMERIC_INTERNAL;}catch(...){message(error,capacity,"Unknown inference task failure.");return PENTA_NUMERIC_INTERNAL;}}
};
int Curve::evaluate(const double* point,uint64_t n,double* residual,uint64_t m,double* jacobian,char*,uint64_t){if(n!=free.size()||m!=x.size())throw std::runtime_error("Native refit residual layout mismatch.");for(uint64_t j=0;j<n;++j)full[free[j]]=point[j];std::vector<double> derivatives(experiment.request.dimension);for(uint64_t i=0;i<m;++i){const auto noise=penta::inference::noise::residual(static_cast<penta::inference::noise::Model>(experiment.request.fit.reserved),experiment.prediction(full.data(),x[i],jacobian?derivatives.data():nullptr),y[i],experiment.request.fit.noise_variance,experiment.request.fit.loss_scale);residual[i]=noise.value;if(jacobian)for(uint64_t j=0;j<n;++j)jacobian[i*n+j]=noise.derivative*derivatives[free[j]];}return 0;}
double quantile(std::vector<double>& values,double p){std::sort(values.begin(),values.end());const double x=p*(values.size()-1);const auto i=static_cast<std::size_t>(x);return values[i]+(x-i)*(values[std::min(i+1,values.size()-1)]-values[i]);}
void summary(const penta_inference_request_v1& r,penta_inference_result_v1& out){
    out.completed=out.successful=0;out.best_task=0;out.statistics_valid=0;double best=std::numeric_limits<double>::infinity();std::vector<uint64_t> successful;
    for(uint64_t row=0;row<r.runs;++row){if(out.status[row]!=PENTA_INF_PENDING)++out.completed;if(out.status[row]==PENTA_INF_CONVERGED||out.status[row]==PENTA_INF_RANK_DEFICIENT){if(out.status[row]==PENTA_INF_CONVERGED)successful.push_back(row);++out.successful;if(out.costs[row]<best){best=out.costs[row];out.best_task=row+1;}}}
    if(r.operation!=PENTA_INF_BOOTSTRAP||successful.size()<2)return;
    const uint64_t n=r.dimension,k=r.prediction_count,count=successful.size();std::fill_n(out.parameter_mean,n,0);
    for(const auto row:successful)for(unsigned j=0;j<n;++j)out.parameter_mean[j]+=out.parameters[row*n+j]/count;
    std::vector<long double> covariance(n*n,0);
    for(const auto row:successful)for(unsigned j=0;j<n;++j)for(unsigned l=0;l<n;++l)covariance[j*n+l]+=(static_cast<long double>(out.parameters[row*n+j])-out.parameter_mean[j])*(static_cast<long double>(out.parameters[row*n+l])-out.parameter_mean[l])/(count-1);
    for(unsigned j=0;j<n;++j){out.parameter_std[j]=std::sqrt(static_cast<double>(covariance[j*n+j]));if(!std::isfinite(out.parameter_std[j])){std::fill_n(out.parameter_std,n,0);return;}}
    for(unsigned j=0;j<n;++j)for(unsigned l=0;l<n;++l){const bool defined=out.parameter_std[j]>0&&out.parameter_std[l]>0;out.correlation_defined[j*n+l]=defined?1:0;out.correlation[j*n+l]=defined?static_cast<double>(covariance[j*n+l]/(static_cast<long double>(out.parameter_std[j])*out.parameter_std[l])):0;}
    std::vector<double> values(count);const double tail=(1-r.confidence)/2;for(uint64_t j=0;j<k;++j){for(std::size_t i=0;i<count;++i)values[i]=out.predictions[successful[i]*k+j];out.fitted_lower[j]=quantile(values,tail);out.fitted_upper[j]=quantile(values,1-tail);}out.statistics_valid=1;
}
int run(const penta_inference_host_v1* host,const penta_inference_request_v1* request,penta_inference_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_INFERENCE_ABI_V1||!host->fit||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw std::runtime_error("Incompatible inference ABI.");
        const auto& r=*request;auto& out=*result;const uint64_t n=r.dimension,k=r.prediction_count,m=r.observations;
        if(!n||n>16||m<2||m>4096||!k||k>4096||!r.runs||r.runs>4096||r.runs*(n+k+4)>8000000||r.operation>3||r.bootstrap_method>3||!r.initial||!r.x||!r.y||!r.prediction_x||!r.model||!r.batch_size||r.batch_size>256||!std::isfinite(r.confidence)||r.confidence<=0||r.confidence>=1||!std::isfinite(r.max_wall_seconds)||r.max_wall_seconds<=0||out.capacity<r.runs||out.prediction_capacity<r.runs*k||!out.parameters||!out.costs||!out.predictions||!out.base_parameters||!out.base_predictions||!out.status||!out.iterations||!out.evaluations||!out.ranks||!out.messages||!out.parameter_mean||!out.parameter_std||!out.correlation||!out.correlation_defined||!out.fitted_lower||!out.fitted_upper||r.fit.loss||r.fit.covariance||r.fit.weights)throw std::runtime_error("Invalid native inference dimensions, objective or output layout.");
        if(r.fit.struct_size<sizeof(r.fit)||r.fit.method<1||r.fit.method>2||!r.fit.max_iterations||!r.fit.max_evaluations||!std::isfinite(r.fit.noise_variance)||(r.fit.noise_variance!=-1&&r.fit.noise_variance<=0))throw std::runtime_error("Invalid inference fitting template/objective.");
        for(const auto v:{r.fit.loss_scale,r.fit.function_tolerance,r.fit.gradient_tolerance,r.fit.parameter_tolerance,r.fit.maximum_wall_seconds})if(!std::isfinite(v)||v<=0)throw std::runtime_error("Inference fit tolerances/wall budget must be positive.");
        for(unsigned j=0;j<n;++j)if((r.fit.lower&&std::isnan(r.fit.lower[j]))||(r.fit.upper&&std::isnan(r.fit.upper[j]))||(r.fit.lower&&r.fit.upper&&r.fit.lower[j]>=r.fit.upper[j]))throw std::runtime_error("Inference parameter bounds must be strictly ordered.");
        if(((r.operation==0||r.operation==3)&&!r.starts)||(r.operation==1&&(!r.profile_grid||r.profile_coordinate>=n))||(r.operation==2&&r.bootstrap_method==2&&(r.block_length<2||r.block_length>m)))throw std::runtime_error("Invalid starts, profile coordinate/grid or moving-block length.");
        for(uint64_t i=0;i<m;++i)if(!std::isfinite(r.x[i])||!std::isfinite(r.y[i]))throw std::runtime_error("Inference observations must be finite.");
        for(uint64_t i=0;i<k;++i)if(!std::isfinite(r.prediction_x[i]))throw std::runtime_error("Inference prediction coordinates must be finite.");
        for(uint64_t row=0;row<r.runs;++row){if(out.status[row]>PENTA_INF_RANK_DEFICIENT)throw std::runtime_error("Invalid resumed task status.");for(uint64_t j=0;j<n;++j){const auto v=(r.operation==0||r.operation==3)?r.starts[row*n+j]:r.initial[j];if(!std::isfinite(v)||(r.fit.lower&&v<r.fit.lower[j])||(r.fit.upper&&v>r.fit.upper[j]))throw std::runtime_error("Inference starts must be finite and inside bounds.");}if(r.operation==1&&(!std::isfinite(r.profile_grid[row])||(r.fit.lower&&r.profile_grid[row]<r.fit.lower[r.profile_coordinate])||(r.fit.upper&&r.profile_grid[row]>r.fit.upper[r.profile_coordinate])))throw std::runtime_error("Profile grid must lie within the selected parameter bounds.");}
        if(r.fit.reserved>3)throw std::runtime_error("Unknown inference noise model.");
        if(r.fit.reserved)penta::inference::noise::validate(static_cast<penta::inference::noise::Model>(r.fit.reserved),r.fit.noise_variance,r.fit.loss_scale);
        if(r.operation==2&&r.fit.reserved==3&&r.bootstrap_method!=0&&r.bootstrap_method!=3)throw std::runtime_error("Poisson bootstrap uses pairs or explicit parametric count sampling.");
        if(r.operation==2&&r.bootstrap_method==3)penta::inference::noise::validate(static_cast<penta::inference::noise::Model>(r.fit.reserved),r.fit.noise_variance,r.fit.loss_scale);
        Experiment experiment{*host,r,out};out.cancelled=out.user_stopped=0;
        auto checkpoint=[&]{summary(r,out);out.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-experiment.started).count();if(host->checkpoint&&host->checkpoint(host->context,&out,error,capacity))throw std::runtime_error(error&&error[0]?error:"Inference checkpoint failed.");};
        try{
        if(r.operation==3&&!out.base_ready){if(r.fit.reserved!=0&&r.fit.reserved!=1)throw std::runtime_error("Coverage studies currently require Gaussian or lognormal known-variance observation noise.");penta::inference::noise::validate(static_cast<penta::inference::noise::Model>(r.fit.reserved),r.fit.noise_variance,r.fit.loss_scale);std::copy_n(r.initial,n,out.base_parameters);for(uint64_t i=0;i<m;++i)out.base_predictions[i]=experiment.prediction(r.initial,r.x[i]);out.base_ready=1;out.base_rank=n;out.base_cost=0;}
        if(r.operation!=0&&r.operation!=3&&!out.base_ready){auto curve=experiment.ordinary(r.initial);const auto fit=experiment.fit(curve,std::vector<double>(r.initial,r.initial+n));if(!fit.converged)throw std::runtime_error("Baseline fit must converge before profiling/bootstrap refits.");std::copy(fit.parameters.begin(),fit.parameters.end(),out.base_parameters);out.base_cost=fit.cost;out.base_rank=fit.rank;message(out.base_message,sizeof(out.base_message),fit.message.c_str());for(uint64_t i=0;i<m;++i)out.base_predictions[i]=experiment.prediction(fit.parameters.data(),r.x[i]);out.base_ready=1;}
        checkpoint();
        for(uint64_t offset=0;offset<r.runs;offset+=r.batch_size){
            if(host->cancelled&&host->cancelled(host->context)){out.cancelled=1;break;}if(host->stopped&&host->stopped(host->context))throw CleanStop{};experiment.check();const auto end=std::min(r.runs,offset+r.batch_size);int status=0;
            if(host->parallel_for){
                struct Batch{Experiment& experiment;uint64_t offset;static int run(void* raw,uint64_t begin,uint64_t end,char* e,uint64_t c)noexcept{auto& b=*static_cast<Batch*>(raw);return Experiment::tasks(&b.experiment,b.offset+begin,b.offset+end,e,c);}}batch{experiment,offset};status=host->parallel_for(host->context,end-offset,4096,Batch::run,&batch,error,capacity);
            }else status=Experiment::tasks(&experiment,offset,end,error,capacity);
            if(status==PENTA_NUMERIC_CANCELLED)out.cancelled=1;
            if(status==PENTA_INFERENCE_STOPPED)out.user_stopped=1;
            checkpoint();if(status&&status!=PENTA_NUMERIC_CANCELLED&&status!=PENTA_INFERENCE_STOPPED)throw std::runtime_error(error&&error[0]?error:"Native inference tasks failed.");if(out.cancelled||out.user_stopped)break;
        }
        }catch(const CleanStop&){out.user_stopped=1;}
        checkpoint();return 0;
    }catch(const std::exception& failure){message(error,capacity,failure.what());return 1;}catch(...){message(error,capacity,"Unknown inference failure.");return 4;}
}
int runLegacy(const penta_inference_host_v1* host,const penta_inference_request_v1* request,penta_inference_result_v1* result,char* error,uint64_t capacity)noexcept{
    if(request&&(request->fit.reserved||request->operation>2||request->bootstrap_method>2)){message(error,capacity,"New likelihood/coverage requests require the version2 inference entry point.");return PENTA_NUMERIC_ARGUMENT;}
    return run(host,request,result,error,capacity);
}
const penta_inference_api_v1 api{sizeof(api),PENTA_INFERENCE_ABI_V1,runLegacy};
const penta_inference_api_v2 api2{sizeof(api2),PENTA_INFERENCE_ABI_V2,run};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_inference_api_v1*penta_inference_query_v1(uint32_t version){return version==PENTA_INFERENCE_ABI_V1?&api:nullptr;}
extern "C" PENTA_NUMERIC_EXPORT const penta_inference_api_v2*penta_inference_query_v2(uint32_t version){return version==PENTA_INFERENCE_ABI_V2?&api2:nullptr;}
