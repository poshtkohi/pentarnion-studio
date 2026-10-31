#ifndef PENTA_EVOLUTION_API_H
#define PENTA_EVOLUTION_API_H
#include "numeric_api.h"
#include "iteration_api.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_EVOLUTION_ABI_V1 0x00010000u
#if defined(_WIN32) && defined(PENTA_EVOLUTION_MODULE_BUILD)
#define PENTA_EVOLUTION_EXPORT __declspec(dllexport)
#elif !defined(_WIN32)
#define PENTA_EVOLUTION_EXPORT __attribute__((visibility("default")))
#else
#define PENTA_EVOLUTION_EXPORT
#endif
enum penta_evolution_status_v1 {PENTA_EVOLUTION_OK=0,PENTA_EVOLUTION_ARGUMENT=1,PENTA_EVOLUTION_DOMAIN=2,PENTA_EVOLUTION_CANCELLED=3,PENTA_EVOLUTION_INTERNAL=4,PENTA_EVOLUTION_VERSION=5,PENTA_EVOLUTION_BUDGET=6,PENTA_EVOLUTION_CALLBACK=7};
typedef int (*penta_evolution_evaluate_v1)(void*,const double*,uint64_t,double*,uint64_t,char*,uint64_t);
typedef struct penta_evolution_host_v1 {
    uint32_t struct_size,abi_version;void* context;
    int (*cancelled)(void*);
    int (*charge)(void*,uint64_t,char*,uint64_t);
    int (*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
    penta_iteration_observer_v1 output;
} penta_evolution_host_v1;
/* algorithm 1=jDE,2=iDE,3=CMA-ES,4=real-coded elitist GA,5=NSGA-II.
 * Finite strict box bounds only; all objectives are minimized. Physical coordinates
 * are mapped to [0,1], optionally logarithmically. Matrices use row-major buffers. */
typedef struct penta_evolution_request_v1 {
    uint32_t struct_size,algorithm,seed,population,restarts,target_enabled;
    uint64_t dimension,objectives,max_generations,max_evaluations,history_capacity;
    const double *initial,*lower,*upper;const uint8_t* log_coordinates;
    double maximum_wall_seconds,ftol,xtol,target_objective;
    void* callback_context;penta_evolution_evaluate_v1 evaluate;
} penta_evolution_request_v1;
typedef struct penta_evolution_result_v1 {
    uint32_t struct_size,converged,restarts_completed,reserved;
    uint64_t evaluations,generations,history_rows,population_rows,pareto_rows;
    double objective_value,elapsed_seconds;char status[96],message[512];
    double *solution,*history,*population_x,*population_f,*pareto_x,*pareto_f;
    uint64_t solution_capacity,history_capacity,population_x_capacity,population_f_capacity,pareto_x_capacity,pareto_f_capacity;
} penta_evolution_result_v1;
typedef struct penta_evolution_api_v1 {uint32_t struct_size,abi_version;const char* module_name;const char* build_identity;uint64_t capabilities;int(*solve)(const penta_evolution_host_v1*,const penta_evolution_request_v1*,penta_evolution_result_v1*,char*,uint64_t);} penta_evolution_api_v1;
typedef const penta_evolution_api_v1* (*penta_evolution_query_function_v1)(uint32_t);
PENTA_EVOLUTION_EXPORT const penta_evolution_api_v1* penta_evolution_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
