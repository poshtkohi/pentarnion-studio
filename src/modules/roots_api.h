#ifndef PENTA_ROOTS_API_H
#define PENTA_ROOTS_API_H
#include <stdint.h>
#include "iteration_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_ROOTS_ABI_V1 0x00010000u
#if defined(_WIN32) && defined(PENTA_ROOTS_MODULE_BUILD)
#define PENTA_ROOTS_EXPORT __declspec(dllexport)
#elif !defined(_WIN32)
#define PENTA_ROOTS_EXPORT __attribute__((visibility("default")))
#else
#define PENTA_ROOTS_EXPORT
#endif
enum penta_roots_status_v1 {
    PENTA_ROOTS_OK=0,PENTA_ROOTS_ARGUMENT=1,PENTA_ROOTS_DOMAIN=2,
    PENTA_ROOTS_CANCELLED=3,PENTA_ROOTS_INTERNAL=4,PENTA_ROOTS_VERSION=5,
    PENTA_ROOTS_BUDGET=6,PENTA_ROOTS_CALLBACK=7
};
enum penta_roots_operation_v1 {PENTA_ROOTS_SCALAR=1,PENTA_ROOTS_SYSTEM=2,PENTA_ROOTS_FIXEDPOINT=3};
/* Borrowed synchronous callback: jacobian is null for a values-only request,
   otherwise output-major dimension-by-dimension. No C++ values/exceptions cross. */
typedef int (*penta_roots_evaluate_v1)(void*,const double*,uint64_t,double*,double*,char*,uint64_t);
typedef struct penta_roots_host_v1 {
    uint32_t struct_size,abi_version;
    void* context;
    int (*cancelled)(void*);
    int (*charge)(void*,uint64_t,char*,uint64_t);
    penta_iteration_observer_v1 output;
} penta_roots_host_v1;
typedef struct penta_roots_request_v1 {
    uint32_t struct_size,operation;
    uint64_t dimension,max_iterations,max_evaluations;
    uint32_t line_search,finite_difference;
    double absolute_tolerance,relative_tolerance,residual_tolerance,maximum_wall_seconds;
    double maximum_newton_step; /* 0: 1000*max(1,scaled initial L2 norm). */
    double lower,upper;
    const double* initial;
    const double* variable_scale;
    const double* residual_scale;
    void* callback_context;
    penta_roots_evaluate_v1 evaluate;
} penta_roots_request_v1;
typedef struct penta_roots_result_v1 {
    uint32_t struct_size,converged;
    int32_t backend_flag;
    uint32_t reserved;
    uint64_t iterations,evaluations,jacobian_evaluations,backend_evaluations;
    double residual_norm,scaled_residual_norm,step_norm,lower,upper;
    char status[128];
    char message[512];
    double* solution;
    uint64_t solution_capacity;
    double* residual;
    uint64_t residual_capacity;
} penta_roots_result_v1;
typedef struct penta_roots_api_v1 {
    uint32_t struct_size,abi_version;
    const char* module_name;
    const char* build_identity;
    uint64_t capabilities;
    int (*solve)(const penta_roots_host_v1*,const penta_roots_request_v1*,penta_roots_result_v1*,char*,uint64_t);
} penta_roots_api_v1;
typedef const penta_roots_api_v1* (*penta_roots_query_function_v1)(uint32_t);
PENTA_ROOTS_EXPORT const penta_roots_api_v1* penta_roots_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
