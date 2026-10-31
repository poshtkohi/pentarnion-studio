#pragma once
#include <stdint.h>
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_PDE_ABI_V1 0x10000u
typedef int (*penta_pde_reaction_v1)(void*,double,double,const double*,double*,char*,uint64_t);
typedef struct penta_pde_host_v1 {
    uint32_t struct_size,abi_version; void* context;
    int (*cancelled)(void*); int (*charge)(void*,uint64_t,char*,uint64_t);
} penta_pde_host_v1;
/* Optional v1 extension. The original prefix remains valid for older callers
   and modules. Parallel callbacks are synchronous and join before returning. */
#define PENTA_PDE_CAP_PARALLEL_V1 UINT64_C(1)
typedef struct penta_pde_parallel_host_v1 {
    penta_pde_host_v1 base;
    uint64_t threads_requested,threads_granted;
    int (*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
} penta_pde_parallel_host_v1;
typedef struct penta_pde_request_v1 {
    uint32_t struct_size,method,boundary,components;
    uint64_t nodes,max_steps,max_reaction_evaluations;
    const double *x,*initial,*left,*right;
    double length,diffusion,t0,t1,time_step,sample_step,max_wall_seconds;
    void* reaction_context; penta_pde_reaction_v1 reaction;
} penta_pde_request_v1;
typedef struct penta_pde_result_v1 {
    uint32_t struct_size; uint64_t samples,steps,reaction_evaluations;
    double *times,*values,*means,*spatial_rms; uint64_t sample_capacity,value_capacity;
    double elapsed_seconds; char status[128],message[512];
} penta_pde_result_v1;
typedef struct penta_pde_api_v1 {
    uint32_t struct_size,abi_version;
    int (*solve)(const penta_pde_host_v1*,const penta_pde_request_v1*,penta_pde_result_v1*,char*,uint64_t);
} penta_pde_api_v1;
typedef struct penta_pde_parallel_api_v1 {
    penta_pde_api_v1 base;
    uint64_t capabilities;
} penta_pde_parallel_api_v1;
typedef struct penta_geometry_request_v1 {
    uint32_t struct_size,operation;
    uint64_t nx,ny,nz,level_count;
    const double *x,*y,*z,*values,*levels;
} penta_geometry_request_v1;
typedef struct penta_geometry_result_v1 {
    uint32_t struct_size;
    uint64_t points,triangles,segments,point_capacity,triangle_capacity,segment_capacity;
    double *xyz,*scalars;uint32_t *faces,*edges;
} penta_geometry_result_v1;
typedef int (*penta_geometry_build_v1)(const penta_pde_host_v1*,const penta_geometry_request_v1*,penta_geometry_result_v1*,char*,uint64_t);
typedef const penta_pde_api_v1* (*penta_pde_query_function_v1)(uint32_t);
#ifdef __cplusplus
}
#endif
