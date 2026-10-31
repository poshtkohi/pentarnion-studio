#pragma once
#include "fem_parallel_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_FEM_SYSTEM_ABI_V1 0x10000u
enum {PENTA_FEM_SYSTEM_DIRICHLET=1,PENTA_FEM_SYSTEM_FLUX=2,PENTA_FEM_SYSTEM_ROBIN=3};
typedef int (*penta_fem_system_reaction_v1)(void*,const double*,uint32_t,const double*,uint32_t,double*,double*,char*,uint64_t);
typedef int (*penta_fem_system_field_v1)(void*,const double*,uint32_t,double*,uint64_t,char*,uint64_t);
typedef int (*penta_fem_system_geometry_v1)(void*,const double*,uint32_t,double*,double*,char*,uint64_t);
typedef int (*penta_fem_system_boundary_v1)(void*,uint64_t,const double*,const double*,uint32_t,double*,double*,char*,uint64_t);
typedef struct penta_fem_system_boundary_condition_v1 {uint64_t tag;uint32_t kind,reserved;const double* value;double alpha;} penta_fem_system_boundary_condition_v1;
typedef struct penta_fem_system_request_v1 {
    uint32_t struct_size,dimension,components,order,visualization_refinement,allow_unanchored;
    uint64_t vertices,triangles,boundaries,boundary_conditions,max_newton,max_linear,gmres_restart,max_work,max_bytes;
    const double *points,*parameter_points,*initial,*diffusion_block;
    const uint64_t *faces,*boundary_tags;
    const penta_fem_system_boundary_condition_v1* conditions;
    double diffusion,rtol,atol,linear_tolerance,max_seconds;
    void *reaction_context,*forcing_context,*diffusion_context,*initial_context,*exact_context,*geometry_context,*boundary_context;
    penta_fem_system_reaction_v1 reaction;
    penta_fem_system_field_v1 forcing,diffusion_field,initial_field,exact;
    penta_fem_system_geometry_v1 geometry;
    penta_fem_system_boundary_v1 boundary;
} penta_fem_system_request_v1;
typedef struct penta_fem_system_result_v1 {
    uint32_t struct_size,converged,has_exact,reserved;
    uint64_t dofs,dof_capacity,visualization_vertices,visualization_triangles,vertex_capacity,triangle_capacity;
    double *dof_coordinates,*dof_values,*visualization_coordinates,*visualization_values;
    uint64_t* visualization_faces;
    uint64_t* vertex_dofs; /* optional one-based H1 DOF for each input vertex */
    uint64_t* element_dofs; /* optional triangle-by-local scalar H1 DOFs */
    uint64_t newton_iterations,linear_iterations,reaction_evaluations,jacobian_evaluations,field_evaluations,geometry_evaluations,work_units,estimated_bytes,maximum_nonzeros;
    double residual_norm,relative_residual,l2_error;
    char status[128];
} penta_fem_system_result_v1;
typedef struct penta_fem_system_evaluate_request_v1 {
    uint32_t struct_size,order,components,reserved;
    uint64_t dofs,triangles,queries,max_work,max_bytes;
    const double *values,*reference_points;
    const uint64_t *element_dofs,*face_ids;
    double max_seconds;
} penta_fem_system_evaluate_request_v1;
typedef struct penta_fem_system_evaluate_result_v1 {
    uint32_t struct_size,reserved;
    uint64_t capacity,work_units;
    double* values;
} penta_fem_system_evaluate_result_v1;
typedef int (*penta_fem_time_clock_v1)(void*,double,char*,uint64_t);
typedef int (*penta_fem_time_sample_v1)(void*,double,const double*,uint64_t,uint32_t,char*,uint64_t);
typedef struct penta_fem_time_request_v1 {
    uint32_t struct_size,adaptive;
    penta_fem_system_request_v1 base;
    double t0,t1,time_step,sample_step,minimum_step,maximum_step,rtol,atol;
    uint64_t max_steps;
    void *clock_context,*sample_context;
    penta_fem_time_clock_v1 set_time;
    penta_fem_time_sample_v1 sample;
} penta_fem_time_request_v1;
typedef struct penta_fem_time_result_v1 {
    uint32_t struct_size,reserved;
    penta_fem_system_result_v1 field;
    uint64_t samples,sample_capacity,value_capacity,accepted_steps,rejected_steps;
    double *times,*values;
    double minimum_used_step,maximum_used_step,maximum_error_estimate;
    char status[128];
} penta_fem_time_result_v1;
typedef struct penta_fem_system_api_v1 {
    uint32_t struct_size,abi_version;
    int (*solve)(const penta_fem_host_v1*,const penta_fem_system_request_v1*,penta_fem_system_result_v1*,char*,uint64_t);
    int (*evaluate)(const penta_fem_host_v1*,const penta_fem_system_evaluate_request_v1*,penta_fem_system_evaluate_result_v1*,char*,uint64_t);
    int (*time)(const penta_fem_host_v1*,const penta_fem_time_request_v1*,penta_fem_time_result_v1*,char*,uint64_t);
} penta_fem_system_api_v1;
typedef const penta_fem_system_api_v1* (*penta_fem_system_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_fem_system_api_v1* penta_fem_system_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
