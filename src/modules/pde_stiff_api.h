#pragma once
#include "pde_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_PDE_STIFF_ABI_V1 0x10000u
enum { PENTA_STIFF_FD1=1,PENTA_STIFF_FD2=2,PENTA_STIFF_TRIANGLE_P1=3 };
enum { PENTA_STIFF_DIRICHLET=1,PENTA_STIFF_FLUX=2,PENTA_STIFF_ROBIN=3,PENTA_STIFF_PERIODIC=4 };
/* Every state/coefficient is flattened in its complete real basis. The host
   supplies the compiler-generated LOCAL reaction Jacobian, never user code for
   a global Jacobian. Matrices are row-major, and callbacks are reentrant. */
typedef int (*penta_stiff_reaction_v1)(void*,double,const double*,uint32_t,const double*,uint32_t,double*,double*,char*,uint64_t);
typedef int (*penta_stiff_operator_v1)(void*,double,const double*,uint32_t,double*,double*,char*,uint64_t);
typedef int (*penta_stiff_boundary_v1)(void*,double,uint64_t,const double*,const double*,uint32_t,double*,double*,char*,uint64_t);
/* Driver-thread sample notification: borrowed complete node-major values.
   Return0 continue,1 retain the partial result as user_stopped,<0 error. */
typedef int (*penta_stiff_sample_v1)(void*,double,const double*,uint64_t,uint32_t,char*,uint64_t);
/* Optional host-owned lazy sparse solver. Output is a candidate only: the PDE
   kernel independently verifies the original Newton-system residual. */
typedef int (*penta_stiff_linear_v1)(void*,uint64_t,uint64_t,const uint64_t*,const uint64_t*,const double*,const double*,uint64_t,double,double*,uint64_t*,uint64_t*,char*,uint64_t);
typedef struct penta_stiff_boundary_edge_v1 {
    uint64_t first,second,tag; /* one-based node IDs; first==second for FD1 */
    uint32_t kind,reserved;
    double robin_alpha;
    const double* value; /* components entries, used without a native callback */
} penta_stiff_boundary_edge_v1;
typedef struct penta_pde_stiff_request_v1 {
    uint32_t struct_size,abi_version,method,dimension,components,adaptive,reserved0,reserved1;
    uint64_t nodes,nx,ny,triangles,boundary_count;
    const double *x,*y,*points,*initial,*diffusion_block,*velocity;
    const uint64_t* faces; /* one-based triangle IDs */
    const penta_stiff_boundary_edge_v1* boundaries;
    double diffusion,t0,t1,time_step,sample_step,minimum_step,maximum_step,rtol,atol;
    double newton_tolerance,linear_tolerance,max_seconds;
    uint64_t max_steps,max_newton,max_linear,gmres_restart,max_work,max_bytes;
    void *reaction_context,*operator_context,*boundary_context;
    penta_stiff_reaction_v1 reaction;
    penta_stiff_operator_v1 coefficient;
    penta_stiff_boundary_v1 boundary;
    void* sample_context;
    penta_stiff_sample_v1 sample;
    void* linear_context;
    penta_stiff_linear_v1 linear;
} penta_pde_stiff_request_v1;
typedef struct penta_pde_stiff_result_v1 {
    uint32_t struct_size,reserved;
    uint64_t samples,sample_capacity,value_capacity;
    double *times,*values,*means,*spatial_rms;
    uint64_t accepted_steps,rejected_steps,newton_iterations,linear_iterations;
    uint64_t reaction_evaluations,jacobian_evaluations,operator_evaluations,maximum_nonzeros,work_units;
    double minimum_used_step,maximum_used_step,maximum_error_estimate,elapsed_seconds;
    char status[128],message[512];
} penta_pde_stiff_result_v1;
typedef struct penta_pde_stiff_api_v1 {
    uint32_t struct_size,abi_version;
    int (*solve)(const penta_pde_host_v1*,const penta_pde_stiff_request_v1*,penta_pde_stiff_result_v1*,char*,uint64_t);
} penta_pde_stiff_api_v1;
typedef const penta_pde_stiff_api_v1* (*penta_pde_stiff_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_pde_stiff_api_v1* penta_pde_stiff_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
