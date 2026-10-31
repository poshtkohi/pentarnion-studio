#define PENTA_CHAOS_MODULE_BUILD
#include "chaos_api.h"
#include <Eigen/QR>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <vector>
namespace {
int message(int status,char* out,uint64_t size,const char* text) noexcept {
    if(out&&size){const auto n=std::min<uint64_t>(size-1,std::strlen(text));std::memcpy(out,text,static_cast<std::size_t>(n));out[n]=0;}return status;
}
int qr(penta_chaos_qr_v1* request,char* error,uint64_t capacity) noexcept {
    try {
        if(!request||request->struct_size<sizeof(*request)||request->dimension<1||request->dimension>128||!request->tangent||!request->basis||!request->logarithms||request->basis_capacity<request->dimension*request->dimension||request->logarithms_capacity<request->dimension)
            return message(PENTA_CHAOS_ARGUMENT,error,capacity,"Invalid real-coordinate tangent QR buffers or dimension.");
        const auto n=static_cast<Eigen::Index>(request->dimension);
        Eigen::MatrixXd tangent(n,n);
        for(Eigen::Index row=0;row<n;++row)for(Eigen::Index col=0;col<n;++col){const auto v=request->tangent[row*n+col];if(!std::isfinite(v))return message(PENTA_CHAOS_DOMAIN,error,capacity,"Nonfinite tangent matrix; reduce the QR interval or check the model.");tangent(row,col)=v;}
        Eigen::HouseholderQR<Eigen::MatrixXd> factor(tangent);
        const Eigen::MatrixXd basis=factor.householderQ()*Eigen::MatrixXd::Identity(n,n);
        std::vector<double> logs(static_cast<std::size_t>(n));
        for(Eigen::Index col=0;col<n;++col){const double diagonal=std::abs(factor.matrixQR()(col,col));if(!(diagonal>std::numeric_limits<double>::min())||!std::isfinite(diagonal))return message(PENTA_CHAOS_DOMAIN,error,capacity,"Tangent direction collapsed numerically; reduce qr_step and/or tighten solver tolerances.");logs[static_cast<std::size_t>(col)]=std::log(diagonal);}
        const double orthogonality=(basis.transpose()*basis-Eigen::MatrixXd::Identity(n,n)).norm();
        if(!std::isfinite(orthogonality))return message(PENTA_CHAOS_DOMAIN,error,capacity,"Tangent QR produced nonfinite orthogonality diagnostics.");
        for(Eigen::Index row=0;row<n;++row)for(Eigen::Index col=0;col<n;++col)request->basis[row*n+col]=basis(row,col);
        std::copy(logs.begin(),logs.end(),request->logarithms);request->orthogonality_error=orthogonality;
        return PENTA_CHAOS_OK;
    }catch(const std::exception& failure){return message(PENTA_CHAOS_INTERNAL,error,capacity,failure.what());}catch(...){return message(PENTA_CHAOS_INTERNAL,error,capacity,"Native chaos QR failed.");}
}
int recurrence(penta_chaos_recurrence_v1* request,char* error,uint64_t capacity) noexcept {
    try {
        if(!request||request->struct_size<sizeof(*request)||request->samples<2||request->samples>256||request->dimension<1||request->dimension>128||!request->states||!request->matrix||request->matrix_capacity<request->samples*request->samples||!std::isfinite(request->radius)||request->radius<=0||request->theiler>=request->samples)
            return message(PENTA_CHAOS_ARGUMENT,error,capacity,"Recurrence requires 2..256 samples, 1..128 real coordinates, positive radius and valid Theiler exclusion.");
        const auto rows=request->samples,n=request->dimension;
        for(uint64_t k=0;k<rows*n;++k)if(!std::isfinite(request->states[k]))return message(PENTA_CHAOS_DOMAIN,error,capacity,"Recurrence coordinates must be finite.");
        std::vector<double> output(static_cast<std::size_t>(rows*rows),0);uint64_t eligible=0,recurrent=0;
        for(uint64_t row=0;row<rows;++row)for(uint64_t col=row+1;col<rows;++col){if(col-row<=request->theiler)continue;double distance=0;for(uint64_t k=0;k<n;++k)distance=std::hypot(distance,request->states[row*n+k]-request->states[col*n+k]);eligible+=2;if(distance<=request->radius){output[row*rows+col]=output[col*rows+row]=1;recurrent+=2;}}
        std::copy(output.begin(),output.end(),request->matrix);request->eligible=eligible;request->recurrent=recurrent;return PENTA_CHAOS_OK;
    }catch(const std::exception& failure){return message(PENTA_CHAOS_INTERNAL,error,capacity,failure.what());}catch(...){return message(PENTA_CHAOS_INTERNAL,error,capacity,"Native recurrence analysis failed.");}
}
const penta_chaos_api_v1 api{sizeof(api),PENTA_CHAOS_ABI_V1,"penta_chaos","tangent-qr-recurrence-v1",3,qr,recurrence};
}
extern "C" PENTA_CHAOS_EXPORT const penta_chaos_api_v1* penta_chaos_query_v1(uint32_t abi){return abi==PENTA_CHAOS_ABI_V1?&api:nullptr;}
