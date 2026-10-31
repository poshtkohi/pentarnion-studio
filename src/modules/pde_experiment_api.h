#pragma once
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_PDE_EXPERIMENT_ABI_V1 0x00010000u
typedef int(*penta_pde_experiment_reaction_v1)(void*,uint64_t,double,double,double,const double*,double*,char*,uint64_t);
enum penta_pde_experiment_status_v1{PENTA_PDE_RUN_PENDING=0,PENTA_PDE_RUN_COMPLETED=1,PENTA_PDE_RUN_FAILED=2,PENTA_PDE_RUN_TIMEOUT=3};
typedef struct penta_pde_run_v1{
    uint32_t struct_size;uint64_t samples,nodes,components,steps,reaction_evaluations,linear_solves,factorizations,threads_used;
    const double *times,*values,*means,*rms;void* handle;
}penta_pde_run_v1;
typedef struct penta_pde_experiment_request_v1{
    uint32_t struct_size,components;uint64_t runs,nodes,batch_size;double max_wall_seconds;
}penta_pde_experiment_request_v1;
typedef struct penta_pde_experiment_result_v1{
    uint32_t struct_size,cancelled;uint64_t capacity,completed;
    uint32_t* status;double *final_values,*final_means,*final_rms,*maximum_rms,*stop_times;
    uint64_t *steps,*evaluations,*linear_solves,*factorizations,*inner_threads;
    char* messages;double elapsed_seconds;
}penta_pde_experiment_result_v1;
typedef struct penta_pde_experiment_host_v1{
    uint32_t struct_size,abi_version;void* context;
    int(*cancelled)(void*);
    int(*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
    int(*solve)(void*,uint64_t,penta_pde_run_v1*,char*,uint64_t);
    void(*release)(void*,penta_pde_run_v1*);
    int(*checkpoint)(void*,const penta_pde_experiment_result_v1*,char*,uint64_t);
}penta_pde_experiment_host_v1;
typedef struct penta_pde_experiment_api_v1{
    uint32_t struct_size,abi_version;
    int(*run)(const penta_pde_experiment_host_v1*,const penta_pde_experiment_request_v1*,penta_pde_experiment_result_v1*,char*,uint64_t);
}penta_pde_experiment_api_v1;
typedef const penta_pde_experiment_api_v1*(*penta_pde_experiment_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_pde_experiment_api_v1*penta_pde_experiment_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
