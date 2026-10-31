#pragma once
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_MECHANICS_ABI_V1 0x00010000u
enum penta_mechanics_form_v1{PENTA_MECH_NEWTON=0,PENTA_MECH_LAGRANGIAN=1,PENTA_MECH_HAMILTONIAN=2};
enum penta_mechanics_method_v1{PENTA_MECH_RK4=0,PENTA_MECH_VERLET=1,PENTA_MECH_MIDPOINT=2};
/* Scalar model derivatives are over (t,q_1..q_n,second_1..second_n).
 * For Newton, value contains n ordered force coordinates and order is zero. */
typedef int(*penta_mechanics_model_v1)(void*,double,const double*,const double*,uint32_t,double*,double*,double*,char*,uint64_t);
typedef int(*penta_mechanics_force_v1)(void*,double,const double*,const double*,double*,char*,uint64_t);
typedef struct penta_mechanics_host_v1{uint32_t struct_size,abi_version;void* context;int(*cancelled)(void*);int(*charge)(void*,uint64_t,char*,uint64_t);}penta_mechanics_host_v1;
typedef struct penta_mechanics_request_v1{
    uint32_t struct_size,dimension,form,method,position_only,max_newton_iterations;
    const double *initial_q,*initial_second,*masses;
    double t0,t1,time_step,sample_step,max_wall_seconds,newton_tolerance,regularity_tolerance;
    uint64_t max_steps,max_evaluations;
    void* model_context;penta_mechanics_model_v1 model;
    void* force_context;penta_mechanics_force_v1 force;
}penta_mechanics_request_v1;
typedef struct penta_mechanics_result_v1{
    uint32_t struct_size,energy_defined;uint64_t capacity,samples,steps,evaluations,newton_iterations,force_evaluations;
    double *times,*q,*velocity,*momentum,*energy,*model_value,*kinetic;
    double elapsed_seconds,max_relative_energy_drift;
}penta_mechanics_result_v1;
typedef struct penta_mechanics_api_v1{
    uint32_t struct_size,abi_version;
    int(*solve)(const penta_mechanics_host_v1*,const penta_mechanics_request_v1*,penta_mechanics_result_v1*,char*,uint64_t);
}penta_mechanics_api_v1;
typedef const penta_mechanics_api_v1*(*penta_mechanics_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_mechanics_api_v1*penta_mechanics_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
