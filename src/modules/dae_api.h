#pragma once
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_DAE_ABI_V1 0x00010000u
typedef int (*penta_dae_residual_v1)(void*,double,const double*,const double*,double*,double*,double*,char*,uint64_t);
typedef int (*penta_dae_event_v1)(void*,double,const double*,const double*,double*,char*,uint64_t);
typedef int (*penta_dae_reset_v1)(void*,double,const double*,const double*,double*,double*,char*,uint64_t);
typedef struct penta_dae_host_v1 {uint32_t struct_size,abi_version;void* context;int(*cancelled)(void*);int(*charge)(void*,uint64_t,char*,uint64_t);}penta_dae_host_v1;
typedef struct penta_dae_request_v1 {
    uint32_t struct_size,dimension,initialization,jacobian;
    const double *initial,*initial_derivative;const uint8_t* algebraic;
    double t0,t1,sample_step,rtol,atol,initial_residual_tolerance,index_tolerance,max_wall_seconds,initial_step,maximum_step;
    uint64_t max_steps,max_evaluations,max_events;
    void* callback_context;penta_dae_residual_v1 residual;
    void* event_context;penta_dae_event_v1 event;
    void* reset_context;penta_dae_reset_v1 reset;
    int32_t event_direction;uint32_t event_terminal;double minimum_event_interval;
}penta_dae_request_v1;
typedef struct penta_dae_result_v1 {
    uint32_t struct_size,terminal_event;uint64_t capacity,event_capacity,samples,events,steps,residual_evaluations,jacobian_evaluations,event_evaluations;
    double *times,*values,*derivatives,*consistent_initial,*consistent_derivative;
    double *event_times,*event_before,*event_after;
    double elapsed_seconds,initial_residual_norm,initial_correction_norm,maximum_sample_residual;
}penta_dae_result_v1;
typedef struct penta_dae_api_v1 {
    uint32_t struct_size,abi_version;
    int(*solve)(const penta_dae_host_v1*,const penta_dae_request_v1*,penta_dae_result_v1*,char*,uint64_t);
}penta_dae_api_v1;
typedef const penta_dae_api_v1*(*penta_dae_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_dae_api_v1*penta_dae_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
