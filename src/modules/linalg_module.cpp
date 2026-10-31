#define PENTA_LINALG_MODULE_BUILD
#include "linalg_internal.hpp"
#include <cblas.h>
#include <condition_variable>
#include <chrono>
#include <thread>
namespace penta::linalg {
BlasScope::BlasScope(unsigned threads,const penta_numeric_host_v1* host){
    try{scope_.emplace(threads,threads,false,host?host->context:nullptr,host?host->cancelled:nullptr);}
    catch(const blas::Error& failure){throw Error(failure.status,failure.what());}
}
BlasScope::~BlasScope()=default;
namespace {
struct BackendTask {const penta_numeric_host_v1* host;const penta_linalg_request_v1* request;penta_linalg_result_v1* result;};
int backend(void* raw,uint64_t,uint64_t,char* error,uint64_t capacity){return protect([&]{auto& task=*static_cast<BackendTask*>(raw);cancelled(task.host);BlasScope threads(task.request->threads,task.host);task.result->backend_threads=task.request->threads;
    if(task.request->operation>=PENTA_LA_SPARSE_MULTIPLY)sparse(task.host,*task.request,*task.result);
    else if(task.request->operation==PENTA_LA_SVD||task.request->operation==PENTA_LA_EIGEN||task.request->operation==PENTA_LA_SCHUR||task.request->operation==PENTA_LA_RANK||task.request->operation==PENTA_LA_RCOND||task.request->operation==PENTA_LA_COND)spectral(task.host,*task.request,*task.result);
    else denseLinear(task.host,*task.request,*task.result);
    cancelled(task.host);
},error,capacity);}
int compute(const penta_numeric_host_v1* host,const penta_linalg_request_v1* request,penta_linalg_result_v1* result,char* error,uint64_t capacity){return protect([&]{
    require(host&&host->struct_size>=sizeof(*host)&&host->abi_version==PENTA_NUMERIC_ABI_V1&&host->parallel_for&&host->cancelled,"Incompatible linear algebra host.");cancelled(host);
    require(request&&request->struct_size>=sizeof(*request)&&result&&result->struct_size>=sizeof(*result),"Incompatible linear algebra request/result.");
    require(request->threads>=1&&request->threads<=64,"The native host must grant between1 and64 CPU threads.");
    require(std::isfinite(request->gamma)&&std::isfinite(request->rank_tolerance)&&request->rank_tolerance>=0&&std::isfinite(request->rcond_min)&&request->rcond_min>=0&&request->rcond_min<1&&std::isfinite(request->residual_tolerance)&&request->residual_tolerance>0,"Invalid linear algebra tolerance/context.");
    result->converged=0;result->backend_threads=1;result->rank=result->iterations=0;result->scalar=result->residual_norm=result->relative_residual=result->rcond=result->error_estimate=0;
    if(request->operation<PENTA_LA_SOLVE){denseProduct(host,*request,*result);return;}
    if(request->operation>=PENTA_LA_SPARSE_MULTIPLY){sparse(host,*request,*result);return;}
    require(static_cast<long double>(request->a.rows)*request->a.columns*std::min(request->a.rows,request->a.columns)*8<=1000000000.0L,"Factorization exceeds its native arithmetic-work bound.");
    BackendTask task{host,request,result};char local[1024]{};
    const auto status=host->parallel_for(host->context,1,1,backend,&task,local,sizeof(local));
    if(status)throw Error(status,local[0]?local:"Native factorization failed.");
},error,capacity);}
}
}
extern "C" PENTA_LINALG_EXPORT const penta_linalg_api_v1* penta_linalg_query_v1(uint32_t requested){
    static const penta_linalg_api_v1 api{sizeof(penta_linalg_api_v1),PENTA_LINALG_ABI_V1,"penta_linalg","linalg-v1-shared-blas-epochs-svd-reuse",0x7f|PENTA_LINALG_CAP_VALUE_ONLY,&penta::linalg::compute};return requested==PENTA_LINALG_ABI_V1?&api:nullptr;
}
