#pragma once
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_SENSITIVITY_ABI_V1 0x00010000u
enum penta_sensitivity_method_v1 {PENTA_SENS_MORRIS=0,PENTA_SENS_SOBOL=1};
typedef int (*penta_sensitivity_evaluate_v1)(void*,const double*,uint64_t,double*,char*,uint64_t);
typedef struct penta_sensitivity_request_v1 {
    uint32_t struct_size,method,dimensions,levels;
    uint64_t samples,seed,bootstrap,max_evaluations;
    double confidence,max_wall_seconds;
    const double *lower,*upper;
    void* callback_context;penta_sensitivity_evaluate_v1 evaluate;
} penta_sensitivity_request_v1;
typedef struct penta_sensitivity_result_v1 {
    uint32_t struct_size,reserved;uint64_t capacity,evaluations;
    double *mu,*mu_star,*sigma,*first,*total,*first_lower,*first_upper,*total_lower,*total_upper;
    double mean,variance,elapsed_seconds;
} penta_sensitivity_result_v1;
typedef struct penta_sensitivity_api_v1 {
    uint32_t struct_size,abi_version;
    int (*analyze)(const penta_numeric_host_v1*,const penta_sensitivity_request_v1*,penta_sensitivity_result_v1*,char*,uint64_t);
} penta_sensitivity_api_v1;
typedef const penta_sensitivity_api_v1* (*penta_sensitivity_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_sensitivity_api_v1* penta_sensitivity_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
