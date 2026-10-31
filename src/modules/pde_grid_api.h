#pragma once
#include "pde_time_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_PDE_GRID_ABI_V1 0x10000u
typedef int (*penta_pde_grid_reaction_v1)(void*,double,double,double,const double*,double*,char*,uint64_t);
typedef struct penta_pde_grid_request_v1 {
    uint32_t struct_size,abi_version,integrator,boundary,components;
    uint64_t nx,ny,max_steps,max_reaction_evaluations;
    const double *x,*y,*initial,*boundary_value;
    double length_x,length_y,diffusion_x,diffusion_y,t0,t1,time_step,sample_step,max_wall_seconds;
    void* reaction_context;penta_pde_grid_reaction_v1 reaction;
} penta_pde_grid_request_v1;
typedef struct penta_pde_grid_api_v1 {
    uint32_t struct_size,abi_version;
    int (*solve)(const penta_pde_host_v1*,const penta_pde_grid_request_v1*,penta_pde_time_result_v1*,char*,uint64_t);
} penta_pde_grid_api_v1;
typedef const penta_pde_grid_api_v1* (*penta_pde_grid_query_function_v1)(uint32_t);
#ifdef __cplusplus
}
#endif
