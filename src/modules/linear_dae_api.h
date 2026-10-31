#pragma once
#include "dae_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_DAE_ABI_V2 0x00020000u
typedef int(*penta_dae_forcing_v2)(void*,double,uint32_t,double*,char*,uint64_t);
typedef struct penta_dae_analyze_request_v2 {uint32_t struct_size,dimension;const double *jy,*jyp;double rank_tolerance,condition_limit;}penta_dae_analyze_request_v2;
typedef struct penta_dae_analyze_result_v2 {uint32_t struct_size,rank_mass,algebraic_dimension,rank_constraint,supported;int32_t local_index;double condition_mass,condition_constraint;char status[128];}penta_dae_analyze_result_v2;
typedef struct penta_linear_dae_request_v2 {
    uint32_t struct_size,dimension,initialization,maximum_index;
    const double *mass,*linear,*initial;
    double t0,t1,sample_step,rtol,atol,rank_tolerance,condition_limit,residual_tolerance,max_wall_seconds;
    uint64_t max_steps,max_evaluations;
    void* forcing_context;penta_dae_forcing_v2 forcing;
}penta_linear_dae_request_v2;
typedef struct penta_linear_dae_result_v2 {
    uint32_t struct_size,index,differential_dimension,rank_mass,reduction_cache_hit;
    uint64_t capacity,samples,steps,residual_evaluations,jacobian_evaluations,forcing_evaluations;
    double *times,*values,*derivatives,*consistent_initial,*consistent_derivative;
    double *free_basis,*constraint_projector,*reduced_matrix;uint64_t matrix_capacity;
    double initial_correction_norm,maximum_scaled_residual,maximum_reduced_interpolation_residual,closure_residual,condition,operator_scale,elapsed_seconds;
    char status[128];
}penta_linear_dae_result_v2;
typedef struct penta_dae_api_v2 {
    uint32_t struct_size,abi_version;
    int(*analyze)(const penta_dae_host_v1*,const penta_dae_analyze_request_v2*,penta_dae_analyze_result_v2*,char*,uint64_t);
    int(*linear_solve)(const penta_dae_host_v1*,const penta_linear_dae_request_v2*,penta_linear_dae_result_v2*,char*,uint64_t);
}penta_dae_api_v2;
PENTA_NUMERIC_EXPORT const penta_dae_api_v2*penta_dae_query_v2(uint32_t);
#ifdef __cplusplus
}
#endif
