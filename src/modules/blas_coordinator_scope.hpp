#pragma once
#include "blas_coordinator_api.h"
#include <stdexcept>
namespace penta::blas {
struct Error:std::runtime_error{int status;Error(int code,const char* message):std::runtime_error(message),status(code){}};
class Scope{
    const penta_blas_coordinator_api_v1* api_=nullptr;penta_blas_lease_v1 lease_{};
    void check(int status,const char* error){if(status)throw Error(status,error[0]?error:"Shared BLAS coordination failed.");}
public:
    Scope(unsigned threads,unsigned granted,bool exclusive,void* context=nullptr,int(*cancelled)(void*)=nullptr){api_=penta_blas_coordinator_query_v1(PENTA_BLAS_COORDINATOR_ABI_V1);if(!api_||api_->struct_size<sizeof(*api_)||api_->abi_version!=PENTA_BLAS_COORDINATOR_ABI_V1)throw Error(5,"Incompatible shared BLAS coordinator.");lease_.struct_size=sizeof(lease_);penta_blas_host_v1 host{sizeof(host),PENTA_BLAS_COORDINATOR_ABI_V1,granted,0,context,cancelled};penta_blas_request_v1 request{sizeof(request),threads,exclusive?static_cast<uint32_t>(PENTA_BLAS_EXCLUSIVE):0u,0};char error[1024]{};check(api_->begin(&host,&request,&lease_,error,sizeof(error)),error);}
    ~Scope(){if(lease_.token){char error[1]{};(void)api_->end(&lease_,error,sizeof(error));}}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    void reassert(){char error[1024]{};check(api_->reassert(&lease_,error,sizeof(error)),error);}
    void downgrade(){char error[1024]{};check(api_->downgrade(&lease_,error,sizeof(error)),error);}
};
}
