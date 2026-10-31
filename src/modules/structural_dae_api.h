#pragma once
#include "dae_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_DAE_ABI_V3 0x00030000u
typedef struct penta_dae_structure_request_v3 {
    uint32_t struct_size,dimension;
    const int8_t* signature; /* row-major -1 absent,0 y,1 yp; compiler-derived */
    const double *jy,*jyp;double rank_tolerance,condition_limit;
}penta_dae_structure_request_v3;
typedef struct penta_dae_structure_result_v3 {
    uint32_t struct_size,structurally_regular,local_initialization_supported;
    int32_t index_candidate;uint32_t maximum_equation_offset;
    uint64_t capacity;int32_t *equation_offsets,*variable_offsets,*matching;
    uint8_t* algebraic;double initialization_condition;char status[128];
}penta_dae_structure_result_v3;
typedef int(*penta_dae_initial_residual_v3)(void*,double,const double*,const double*,double*,double*,double*,double*,char*,uint64_t);
typedef struct penta_dae_initialize_request_v3 {
    uint32_t struct_size,dimension;const double *initial,*initial_derivative;const uint8_t* algebraic;
    double time,residual_tolerance,rank_tolerance,condition_limit,max_wall_seconds;
    uint64_t max_iterations,max_evaluations;
    void* callback_context;penta_dae_initial_residual_v3 residual;
}penta_dae_initialize_request_v3;
typedef struct penta_dae_initialize_result_v3 {
    uint32_t struct_size;uint64_t capacity,iterations,evaluations;
    double *initial,*initial_derivative,*differential_acceleration;
    double residual_norm,differentiated_residual_norm,correction_norm,initialization_condition,elapsed_seconds;
    char status[128];
}penta_dae_initialize_result_v3;
typedef struct penta_dae_api_v3 {
    uint32_t struct_size,abi_version;
    int(*structure)(const penta_dae_host_v1*,const penta_dae_structure_request_v3*,penta_dae_structure_result_v3*,char*,uint64_t);
    int(*initialize)(const penta_dae_host_v1*,const penta_dae_initialize_request_v3*,penta_dae_initialize_result_v3*,char*,uint64_t);
}penta_dae_api_v3;
PENTA_NUMERIC_EXPORT const penta_dae_api_v3*penta_dae_query_v3(uint32_t);
#ifdef __cplusplus
}
#endif
