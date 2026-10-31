#pragma once
#include "mechanics_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_MECHANICS_CONSTRAINT_ABI_V1 0x00010000u
enum penta_holonomic_method_v1 {PENTA_HOLONOMIC_RATTLE=0,PENTA_HOLONOMIC_SHAKE=1};
/* Autonomous scalar potential or tuple of constraints; derivatives are
 * output-major in the n real flattened configuration coordinates. */
typedef int(*penta_holonomic_function_v1)(void*,const double*,uint32_t,double*,double*,double*,char*,uint64_t);
typedef struct penta_holonomic_request_v1 {
 uint32_t struct_size,dimension,constraints,method,project_initial,max_iterations;
 const double *q0,*v0,*mass;
 double t0,t1,time_step,sample_step,tolerance,regularity_tolerance,max_wall_seconds;
 uint64_t max_steps,max_evaluations,max_work;
 void *potential_context,*constraint_context;
 penta_holonomic_function_v1 potential,constraint;
} penta_holonomic_request_v1;
typedef struct penta_holonomic_result_v1 {
 uint32_t struct_size,reserved;uint64_t capacity,samples,steps,evaluations,iterations,work_units;
 double *times,*q,*v,*p,*energy,*kinetic,*potential,*g,*gv,*lambda;
 double *position_multipliers,*velocity_multipliers,*position_error,*velocity_error,*position_iterations,*correction;
 double initial_q_correction[16],initial_v_correction[16];
 double initial_position_error,initial_velocity_error,max_position_error,max_velocity_error,max_relative_energy_drift,elapsed_seconds;
} penta_holonomic_result_v1;
typedef struct penta_mechanics_constraint_api_v1 {
 uint32_t struct_size,abi_version;
 int(*solve)(const penta_mechanics_host_v1*,const penta_holonomic_request_v1*,penta_holonomic_result_v1*,char*,uint64_t);
} penta_mechanics_constraint_api_v1;
typedef const penta_mechanics_constraint_api_v1*(*penta_mechanics_constraint_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_mechanics_constraint_api_v1*penta_mechanics_constraint_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
