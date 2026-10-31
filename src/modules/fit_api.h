#ifndef PENTA_FIT_API_H
#define PENTA_FIT_API_H
#include <stdint.h>
#include "iteration_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_FIT_ABI_V1 0x00010000u
#if defined(_WIN32) && defined(PENTA_FIT_MODULE_BUILD)
#define PENTA_FIT_EXPORT __declspec(dllexport)
#elif !defined(_WIN32)
#define PENTA_FIT_EXPORT __attribute__((visibility("default")))
#else
#define PENTA_FIT_EXPORT
#endif
enum penta_fit_status_v1 {PENTA_FIT_OK=0,PENTA_FIT_ARGUMENT=1,PENTA_FIT_DOMAIN=2,PENTA_FIT_CANCELLED=3,PENTA_FIT_INTERNAL=4,PENTA_FIT_VERSION=5,PENTA_FIT_BUDGET=6,PENTA_FIT_CALLBACK=7};
/* Evaluate all residual coordinates, optionally their output-major m-by-n
   Jacobian. The host owns compiled callbacks and catches exceptions. */
typedef int (*penta_fit_evaluate_v1)(void*,const double*,uint64_t,double*,uint64_t,double*,char*,uint64_t);
typedef struct penta_fit_host_v1 {uint32_t struct_size,abi_version;void* context;int (*cancelled)(void*);int (*charge)(void*,uint64_t,char*,uint64_t);penta_iteration_observer_v1 output;} penta_fit_host_v1;
typedef struct penta_fit_request_v1 {
    uint32_t struct_size,method,loss,covariance;
    uint64_t dimension,residual_count,start_count,max_iterations,max_evaluations,history_capacity;
    uint32_t threads,reserved;
    const double *initial,*starts,*lower,*upper,*parameter_scale,*weights;
    double loss_scale,function_tolerance,gradient_tolerance,parameter_tolerance,maximum_wall_seconds,noise_variance;
    void* callback_context;penta_fit_evaluate_v1 evaluate;
} penta_fit_request_v1;
/* methods:1 LM,2 traditional dogleg. losses:0 none,1 Huber,2 Cauchy,3 soft_l1.
   All limits cover the whole multistart operation. starts contains start_count
   rows (initial is used only when start_count==0). physical x=scale*z. */
typedef struct penta_fit_result_v1 {
    uint32_t struct_size,converged,covariance_valid,jacobian_available;int32_t backend_flag;
    uint64_t iterations,evaluations,jacobian_evaluations,starts_completed,best_start,rank,history_rows,threads,threads_used;
    double cost,initial_cost,residual_norm,weighted_residual_norm,gradient_norm,rcond,noise_variance,elapsed_seconds;
    char status[96],message[512],covariance_assumption[384];
    double *solution,*residuals,*jacobian,*covariance_matrix,*history;
    uint64_t solution_capacity,residual_capacity,jacobian_capacity,covariance_capacity,history_capacity;
} penta_fit_result_v1;
typedef struct penta_fit_api_v1 {uint32_t struct_size,abi_version;const char* module_name;const char* build_identity;uint64_t capabilities;int (*solve)(const penta_fit_host_v1*,const penta_fit_request_v1*,penta_fit_result_v1*,char*,uint64_t);} penta_fit_api_v1;
typedef const penta_fit_api_v1* (*penta_fit_query_function_v1)(uint32_t);
PENTA_FIT_EXPORT const penta_fit_api_v1* penta_fit_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
