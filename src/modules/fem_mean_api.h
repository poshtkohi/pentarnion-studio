#pragma once
#include "fem_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_FEM_MEAN_ABI_V1 0x10000u
typedef struct penta_fem_mean_request_v1 {penta_fem_request_v1 base;uint32_t abi_version,reserved;double target_mean,compatibility_tolerance;} penta_fem_mean_request_v1;
typedef struct penta_fem_mean_result_v1 {penta_fem_result_v1 base;uint64_t nullspace_components;double compatibility_residual,maximum_mean_error;} penta_fem_mean_result_v1;
typedef struct penta_fem_mean_api_v1 {uint32_t struct_size,abi_version;int (*solve)(const penta_fem_host_v1*,const penta_fem_mean_request_v1*,penta_fem_mean_result_v1*,char*,uint64_t);} penta_fem_mean_api_v1;
typedef const penta_fem_mean_api_v1* (*penta_fem_mean_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_fem_mean_api_v1* penta_fem_mean_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
