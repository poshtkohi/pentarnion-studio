#pragma once
#include "numeric_api.h"
#include "iteration_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_PETSC_ABI_V1 0x10000u
enum {PENTA_PETSC_LINEAR=1,PENTA_PETSC_NONLINEAR=2};
enum {PENTA_PETSC_GMRES=1,PENTA_PETSC_CG=2};
enum {PENTA_PETSC_NONE=0,PENTA_PETSC_JACOBI=1,PENTA_PETSC_ILU=2};
/* Every matrix and residual uses real coordinates. A native callback supplies
   its compiler-generated, output-major Jacobian when jacobian is non-null. */
typedef int (*penta_petsc_evaluate_v1)(void*,const double*,uint64_t,double*,double*,char*,uint64_t);
typedef struct penta_petsc_host_v1 {
    uint32_t struct_size,abi_version;
    void* context;
    int (*cancelled)(void*);
    int (*charge)(void*,uint64_t,char*,uint64_t);
    int (*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
    penta_iteration_observer_v1 output;
    uint64_t threads_granted;
} penta_petsc_host_v1;
typedef struct penta_petsc_request_v1 {
    uint32_t struct_size,operation,method,preconditioner;
    uint64_t dimension,nonzeros,max_iterations,max_evaluations,max_work,max_bytes;
    const uint64_t *row_offsets,*columns; /* sorted, unique, zero-based CSR */
    const double *values,*rhs,*initial;
    double rtol,atol,step_tolerance,max_seconds;
    void* callback_context;
    penta_petsc_evaluate_v1 evaluate;
} penta_petsc_request_v1;
typedef struct penta_petsc_result_v1 {
    uint32_t struct_size,converged;
    int32_t backend_reason;
    uint32_t stopped;
    uint64_t iterations,linear_iterations,evaluations,jacobian_evaluations,work_units;
    double residual_norm,relative_residual,elapsed_seconds;
    double *solution,*residual;
    uint64_t capacity;
    char status[128],message[512];
} penta_petsc_result_v1;
typedef struct penta_petsc_api_v1 {
    uint32_t struct_size,abi_version;
    const char* build_identity;
    int (*solve)(const penta_petsc_host_v1*,const penta_petsc_request_v1*,penta_petsc_result_v1*,char*,uint64_t);
} penta_petsc_api_v1;
typedef const penta_petsc_api_v1* (*penta_petsc_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_petsc_api_v1* penta_petsc_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
