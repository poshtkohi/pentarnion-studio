#include "structural_dae_api.h"
#define EIGEN_DONT_PARALLELIZE
#include <Eigen/SVD>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Matrix=Eigen::MatrixXd;using Vector=Eigen::VectorXd;
void text(char* out,uint64_t cap,const char* value){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,std::strlen(value));std::memcpy(out,value,n);out[n]=0;}}
void check(const penta_dae_host_v1* host,std::size_t work=1){if(host->cancelled&&host->cancelled(host->context))throw std::runtime_error("DAE structural/consistency operation cancelled.");char error[1024]{};if(host->charge&&host->charge(host->context,work,error,sizeof(error)))throw std::runtime_error(error[0]?error:"DAE structural/consistency work budget exceeded.");}
void hostCheck(const penta_dae_host_v1* host){if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_DAE_ABI_V1)throw std::runtime_error("Incompatible DAE structural host ABI.");}
Matrix matrix(const double* values,std::size_t n){Matrix result(n,n);for(std::size_t row=0;row<n;++row)for(std::size_t col=0;col<n;++col){const double value=values[row*n+col];if(!std::isfinite(value))throw std::runtime_error("DAE Jacobian must contain finite values.");result(row,col)=value;}return result;}
struct Factor {
    Matrix value;Vector rowScale,columnScale;Eigen::JacobiSVD<Matrix> svd;double condition=INFINITY;bool regular=false;
    Factor(const Matrix& input,double tolerance,double limit):value(input),rowScale(input.rows()),columnScale(input.cols()){
        // Store divisors: a finite subnormal row may have an unrepresentable
        // reciprocal even though direct division normalizes it exactly.
        for(Eigen::Index row=0;row<value.rows();++row){const double scale=value.row(row).cwiseAbs().maxCoeff();rowScale[row]=scale?scale:1;value.row(row)/=rowScale[row];}
        for(Eigen::Index col=0;col<value.cols();++col){const double scale=value.col(col).cwiseAbs().maxCoeff();columnScale[col]=scale?scale:1;value.col(col)/=columnScale[col];}
        svd=Eigen::JacobiSVD<Matrix>(value,Eigen::ComputeFullU|Eigen::ComputeFullV);const auto s=svd.singularValues();
        if(s.size()&&s[0]>0&&s[s.size()-1]>tolerance*s[0]){condition=s[0]/s[s.size()-1];regular=std::isfinite(condition)&&condition<=limit;}
    }
    Vector solve(const Vector& rhs)const{if(!regular)throw std::runtime_error("DAE initialization matrix is singular, ill-conditioned or rank ambiguous.");const Vector scaled=rhs.array()/rowScale.array();if(!scaled.allFinite())throw std::runtime_error("DAE consistent step cannot be represented after finite row scaling.");const Vector answer=svd.matrixV()*svd.singularValues().cwiseInverse().asDiagonal()*svd.matrixU().transpose()*scaled;const Vector result=answer.array()/columnScale.array();if(!result.allFinite())throw std::runtime_error("DAE consistent step cannot be represented after finite column scaling.");return result;}
};
Matrix initialMatrix(const Matrix& jy,const Matrix& jyp,const uint8_t* algebraic){Matrix result(jy.rows(),jy.cols());for(Eigen::Index col=0;col<jy.cols();++col)result.col(col)=algebraic[col]?jy.col(col):jyp.col(col);return result;}
// Hungarian maximum-weight transversal. Only syntactic signature edges enter
// this matching; numerical Jacobian zeros do not remove compiler dependencies.
std::vector<int> match(const int8_t* signature,std::size_t n,const penta_dae_host_v1* host){
    std::vector<int> u(n+1),v(n+1),p(n+1),way(n+1);constexpr int absent=100000;
    for(std::size_t row=1;row<=n;++row){check(host,n*n);p[0]=static_cast<int>(row);std::size_t col=0;std::vector<int> minimum(n+1,absent*2);std::vector<bool> used(n+1);
        do{used[col]=true;const auto selected=p[col];int delta=absent*2;std::size_t next=0;for(std::size_t j=1;j<=n;++j)if(!used[j]){const auto sigma=signature[(selected-1)*n+j-1];const int reduced=(sigma<0?absent:-int(sigma))-u[selected]-v[j];if(reduced<minimum[j]){minimum[j]=reduced;way[j]=static_cast<int>(col);}if(minimum[j]<delta){delta=minimum[j];next=j;}}for(std::size_t j=0;j<=n;++j)if(used[j]){u[p[j]]+=delta;v[j]-=delta;}else minimum[j]-=delta;col=next;}while(p[col]);
        do{const auto previous=way[col];p[col]=p[previous];col=static_cast<std::size_t>(previous);}while(col);
    }
    std::vector<int> result(n,-1);for(std::size_t col=1;col<=n;++col)if(p[col])result[p[col]-1]=static_cast<int>(col-1);for(std::size_t row=0;row<n;++row)if(result[row]<0||signature[row*n+result[row]]<0)return {};return result;
}
int structure(const penta_dae_host_v1* host,const penta_dae_structure_request_v3* request,penta_dae_structure_result_v3* result,char* error,uint64_t capacity)noexcept{
    try{hostCheck(host);if(!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw std::runtime_error("Incompatible DAE structural ABI.");const auto& q=*request;auto& out=*result;const auto n=q.dimension;
        if(!n||n>32||!q.signature||!q.jy||!q.jyp||out.capacity<n||!out.equation_offsets||!out.variable_offsets||!out.matching||!out.algebraic||!std::isfinite(q.rank_tolerance)||q.rank_tolerance<=0||q.rank_tolerance>=.1||!std::isfinite(q.condition_limit)||q.condition_limit<1)throw std::runtime_error("Invalid bounded DAE structural layout/settings.");
        for(std::size_t k=0;k<n*n;++k)if(q.signature[k]<-1||q.signature[k]>1)throw std::runtime_error("DAE signatures contain only absent(-1), y(0), yp(1).");
        const Matrix jy=matrix(q.jy,n),jyp=matrix(q.jyp,n);out.structurally_regular=out.local_initialization_supported=out.maximum_equation_offset=0;out.index_candidate=-1;out.initialization_condition=0;std::fill_n(out.equation_offsets,n,0);std::fill_n(out.variable_offsets,n,0);std::fill_n(out.matching,n,-1);std::fill_n(out.algebraic,n,0);
        const auto matching=match(q.signature,n,host);if(matching.empty()){text(out.status,sizeof(out.status),"structurally_singular_pattern");return 0;}out.structurally_regular=1;std::vector<int> c(n),d(n);bool stable=false;
        for(std::size_t iteration=0;iteration<=n*n;++iteration){check(host,n*n);for(std::size_t col=0;col<n;++col){d[col]=0;for(std::size_t row=0;row<n;++row)if(q.signature[row*n+col]>=0)d[col]=std::max(d[col],c[row]+q.signature[row*n+col]);}stable=true;for(std::size_t row=0;row<n;++row){const int next=d[matching[row]]-q.signature[row*n+matching[row]];if(next!=c[row])stable=false;c[row]=next;}if(stable)break;}
        if(!stable)throw std::runtime_error("Canonical structural-offset iteration did not converge within its dimension bound.");
        for(std::size_t k=0;k<n;++k){out.equation_offsets[k]=c[k];out.variable_offsets[k]=d[k];out.matching[k]=matching[k];out.algebraic[k]=d[k]==0;out.maximum_equation_offset=std::max(out.maximum_equation_offset,static_cast<uint32_t>(c[k]));}
        out.index_candidate=static_cast<int32_t>(out.maximum_equation_offset+(std::find(d.begin(),d.end(),0)!=d.end()));
        if(out.maximum_equation_offset||out.index_candidate>1){text(out.status,sizeof(out.status),"higher_index_structural_candidate_requires_derivative_array_validation");return 0;}
        for(std::size_t col=0;col<n;++col)if(out.algebraic[col])for(std::size_t row=0;row<n;++row)if(q.signature[row*n+col]==1)throw std::runtime_error("Structural algebraic coordinates unexpectedly contain derivative dependencies.");
        check(host,n*n*n);const Factor factor(initialMatrix(jy,jyp,out.algebraic),q.rank_tolerance,q.condition_limit);out.initialization_condition=std::isfinite(factor.condition)?factor.condition:0;out.local_initialization_supported=factor.regular;
        text(out.status,sizeof(out.status),factor.regular?"structural_candidate_with_nonsingular_local_initialization":"structural_candidate_but_local_initialization_singular_or_ill_conditioned");return 0;
    }catch(const std::exception& e){text(error,capacity,e.what());return 1;}catch(...){text(error,capacity,"Unknown DAE structural failure.");return 4;}
}
int initialize(const penta_dae_host_v1* host,const penta_dae_initialize_request_v3* request,penta_dae_initialize_result_v3* result,char* error,uint64_t capacity)noexcept{
    try{hostCheck(host);if(!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw std::runtime_error("Incompatible DAE consistency ABI.");const auto& q=*request;auto& out=*result;const auto n=q.dimension;
        if(!n||n>32||!q.initial||!q.initial_derivative||!q.algebraic||!q.residual||out.capacity<n||!out.initial||!out.initial_derivative||!out.differential_acceleration||!q.max_iterations||q.max_iterations>100||!q.max_evaluations||q.max_evaluations>10000)throw std::runtime_error("Invalid bounded DAE consistency layout/resource limits.");
        for(const auto value:{q.time,q.residual_tolerance,q.rank_tolerance,q.condition_limit,q.max_wall_seconds})if(!std::isfinite(value))throw std::runtime_error("DAE consistency settings must be finite.");
        if(q.residual_tolerance<=0||q.rank_tolerance<=0||q.rank_tolerance>=.1||q.condition_limit<1||q.max_wall_seconds<=0)throw std::runtime_error("Invalid DAE consistency tolerances.");
        Vector y(n),yp(n);for(std::size_t k=0;k<n;++k){if(!std::isfinite(q.initial[k])||!std::isfinite(q.initial_derivative[k])||q.algebraic[k]>1)throw std::runtime_error("DAE consistency inputs must be finite with a binary mask.");y[k]=q.initial[k];yp[k]=q.initial_derivative[k];}
        std::vector<double> f(n),ft(n),jy(n*n),jyp(n*n);out.evaluations=out.iterations=0;out.correction_norm=out.differentiated_residual_norm=out.initialization_condition=0;const auto started=std::chrono::steady_clock::now();
        auto evaluate=[&](const Vector& state,const Vector& derivative){check(host,n*n*n);if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>q.max_wall_seconds)throw std::runtime_error("DAE consistency wall-time budget exceeded.");if(out.evaluations>=q.max_evaluations)throw std::runtime_error("DAE consistency evaluation budget exceeded.");++out.evaluations;char detail[2048]{};if(q.residual(q.callback_context,q.time,state.data(),derivative.data(),f.data(),ft.data(),jy.data(),jyp.data(),detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"Native DAE consistency callback failed.");for(const auto* values:{&f,&ft,&jy,&jyp})for(const auto value:*values)if(!std::isfinite(value))throw std::runtime_error("Native DAE consistency callback returned a non-finite value.");for(std::size_t col=0;col<n;++col)if(q.algebraic[col])for(std::size_t row=0;row<n;++row)if(jyp[row*n+col]!=0)throw std::runtime_error("An algebraic derivative column is not zero; unsupported consistency partition.");};
        auto norm=[&]{double result=0;for(const auto value:f)result=std::max(result,std::abs(value));return result;};
        evaluate(y,yp);bool converged=false;
        for(std::size_t iteration=0;iteration<=q.max_iterations;++iteration){const Matrix fy=matrix(jy.data(),n),fp=matrix(jyp.data(),n);const Factor factor(initialMatrix(fy,fp,q.algebraic),q.rank_tolerance,q.condition_limit);if(!factor.regular)throw std::runtime_error("DAE consistent initialization requires a nonsingular well-conditioned local index-one matrix.");out.initialization_condition=factor.condition;const double previous=norm();if(previous<=q.residual_tolerance){converged=true;break;}if(iteration==q.max_iterations)break;Vector rhs(n);for(std::size_t k=0;k<n;++k)rhs[k]=-f[k];const Vector step=factor.solve(rhs);const Vector baseY=y,baseYp=yp;bool accepted=false;
            for(unsigned line=0;line<24;++line){const double length=std::ldexp(1.0,-static_cast<int>(line));y=baseY;yp=baseYp;for(std::size_t k=0;k<n;++k)(q.algebraic[k]?y[k]:yp[k])+=length*step[k];evaluate(y,yp);if(norm()<previous*(1-1e-4*length)||norm()<=q.residual_tolerance){accepted=true;break;}}
            if(!accepted)throw std::runtime_error("Native DAE consistency line search failed; no converged state was returned.");
            ++out.iterations;
        }
        if(!converged)throw std::runtime_error("Native DAE consistency Newton iterations did not converge.");
        const Matrix fy=matrix(jy.data(),n),fp=matrix(jyp.data(),n);const Factor factor(initialMatrix(fy,fp,q.algebraic),q.rank_tolerance,q.condition_limit);Vector rhs(n);for(std::size_t row=0;row<n;++row){rhs[row]=-ft[row];for(std::size_t col=0;col<n;++col)if(!q.algebraic[col])rhs[row]-=fy(row,col)*yp[col];}const Vector derivative=factor.solve(rhs);Vector acceleration=Vector::Zero(n);for(std::size_t k=0;k<n;++k)if(q.algebraic[k])yp[k]=derivative[k];else acceleration[k]=derivative[k];
        Vector differentiated=fp*acceleration+fy*yp;for(std::size_t k=0;k<n;++k)differentiated[k]+=ft[k];out.differentiated_residual_norm=differentiated.cwiseAbs().maxCoeff();const double derivativeScale=1+(fp.cwiseAbs()*acceleration.cwiseAbs()+fy.cwiseAbs()*yp.cwiseAbs()).maxCoeff()+Eigen::Map<Vector>(ft.data(),n).cwiseAbs().maxCoeff();if(out.differentiated_residual_norm>1e-10*derivativeScale)throw std::runtime_error("Differentiated DAE consistency equations failed their scaled residual guard.");
        evaluate(y,yp);out.residual_norm=norm();if(out.residual_norm>q.residual_tolerance)throw std::runtime_error("Full consistent derivative changed the original residual beyond tolerance.");for(std::size_t k=0;k<n;++k){out.initial[k]=y[k];out.initial_derivative[k]=yp[k];out.differential_acceleration[k]=acceleration[k];out.correction_norm=std::max({out.correction_norm,std::abs(y[k]-q.initial[k]),std::abs(yp[k]-q.initial_derivative[k])});}out.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();text(out.status,sizeof(out.status),"consistent_residual_and_differentiated_constraints");return 0;
    }catch(const std::exception& e){text(error,capacity,e.what());return 1;}catch(...){text(error,capacity,"Unknown native DAE consistency failure.");return 4;}
}
const penta_dae_api_v3 api{sizeof(api),PENTA_DAE_ABI_V3,structure,initialize};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_dae_api_v3*penta_dae_query_v3(uint32_t version){return version==PENTA_DAE_ABI_V3?&api:nullptr;}
