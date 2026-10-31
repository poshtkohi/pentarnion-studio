#pragma once
#include "fem_system_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_FEM_MIXED_ABI_V1 0x10000u
/* Input/output: [u0..uc-1,dx_u0..,dy_u0..] / [Fx0..,Fy0..,R0..]. */
typedef int (*penta_fem_mixed_flux_v1)(void*,const double*,uint32_t,const double*,double*,double*,char*,uint64_t);
typedef struct penta_fem_mixed_request_v1 {
    uint32_t struct_size,components,visualization_refinement,reserved;
    uint64_t vertices,triangles,boundaries,max_newton,max_linear,gmres_restart,max_work,max_bytes;
    const double* points;const uint64_t *faces,*boundary_tags;const uint32_t* orders;
    double rtol,atol,linear_tolerance,max_seconds;
    penta_fem_mixed_flux_v1 flux;void* flux_context;
    penta_fem_system_field_v1 forcing,initial,dirichlet,exact;
    void *forcing_context,*initial_context,*dirichlet_context,*exact_context;
} penta_fem_mixed_request_v1;
typedef struct penta_fem_mixed_result_v1 {
    uint32_t struct_size,converged,has_exact,reserved;
    uint64_t dofs,dof_capacity,visualization_vertices,visualization_triangles,vertex_capacity,triangle_capacity;
    double *coordinates,*values,*visualization_coordinates,*visualization_values;
    uint64_t *component_offsets,*visualization_faces;
    uint64_t newton_iterations,linear_iterations,flux_evaluations,jacobian_evaluations,field_evaluations,work_units,estimated_bytes,maximum_nonzeros;
    double residual_norm,relative_residual,l2_error;
    char status[128];
} penta_fem_mixed_result_v1;
typedef struct penta_fem_mixed_api_v1 {
    uint32_t struct_size,abi_version;
    int (*solve)(const penta_fem_host_v1*,const penta_fem_mixed_request_v1*,penta_fem_mixed_result_v1*,char*,uint64_t);
} penta_fem_mixed_api_v1;
typedef const penta_fem_mixed_api_v1* (*penta_fem_mixed_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_fem_mixed_api_v1* penta_fem_mixed_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
