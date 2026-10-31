#pragma once
#include "fem_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_FEM_CAP_PARALLEL_CG_V1 UINT64_C(1)
typedef struct penta_fem_parallel_host_v1 {penta_fem_host_v1 base;uint64_t threads_requested,threads_granted;int (*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);} penta_fem_parallel_host_v1;
typedef struct penta_fem_parallel_api_v1 {penta_fem_api_v1 base;uint64_t capabilities;} penta_fem_parallel_api_v1;
#ifdef __cplusplus
}
#endif
