#ifndef PENTA_OPT_API_H
#define PENTA_OPT_API_H
#include <stdint.h>
#include "iteration_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_OPT_ABI_V1 0x00010000u
#if defined(_WIN32) && defined(PENTA_OPT_MODULE_BUILD)
#define PENTA_OPT_EXPORT __declspec(dllexport)
#elif !defined(_WIN32)
#define PENTA_OPT_EXPORT __attribute__((visibility("default")))
#else
#define PENTA_OPT_EXPORT
#endif
enum penta_opt_status_v1 {PENTA_OPT_OK=0,PENTA_OPT_ARGUMENT=1,PENTA_OPT_DOMAIN=2,PENTA_OPT_CANCELLED=3,PENTA_OPT_INTERNAL=4,PENTA_OPT_VERSION=5,PENTA_OPT_BUDGET=6,PENTA_OPT_CALLBACK=7};
typedef int (*penta_opt_evaluate_v1)(void*,const double*,uint64_t,double*,uint64_t,double*,char*,uint64_t);
typedef struct penta_opt_host_v1 {uint32_t struct_size,abi_version;void* context;int (*cancelled)(void*);int (*charge)(void*,uint64_t,char*,uint64_t);penta_iteration_observer_v1 output;} penta_opt_host_v1;
/* algorithm:1 LBFGS,2 BOBYQA,3 Nelder-Mead,4 SLSQP,5 COBYLA,
   6 DIRECT-L,7 CRS2-LM,8 ISRES. Constraints use g<=0 and h==0. */
typedef struct penta_opt_request_v1 {
    uint32_t struct_size,algorithm,seed,population;
    uint64_t dimension,inequality_count,equality_count,max_evaluations,history_capacity;
    const double *initial,*lower,*upper,*parameter_scale;
    double ftol_relative,ftol_absolute,xtol_relative,xtol_absolute,maximum_wall_seconds,constraint_tolerance,initial_step;
    void *objective_context,*inequality_context,*equality_context;
    penta_opt_evaluate_v1 objective,inequality,equality;
} penta_opt_request_v1;
typedef struct penta_opt_result_v1 {
    uint32_t struct_size,converged,feasible,reserved;int32_t backend_flag;
    uint64_t evaluations,objective_evaluations,constraint_evaluations,gradient_evaluations,history_rows;
    double objective_value,constraint_violation,elapsed_seconds;
    char status[96],message[512];
    double *solution,*history;uint64_t solution_capacity,history_capacity;
} penta_opt_result_v1;
typedef struct penta_opt_api_v1 {uint32_t struct_size,abi_version;const char* module_name;const char* build_identity;uint64_t capabilities;int (*solve)(const penta_opt_host_v1*,const penta_opt_request_v1*,penta_opt_result_v1*,char*,uint64_t);} penta_opt_api_v1;
typedef const penta_opt_api_v1* (*penta_opt_query_function_v1)(uint32_t);
PENTA_OPT_EXPORT const penta_opt_api_v1* penta_opt_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
