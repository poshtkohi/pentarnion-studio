#ifndef PENTA_FEM_API_H
#define PENTA_FEM_API_H
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_FEM_ABI_V1 0x00010000u
typedef int (*penta_fem_field_v1)(void*,double,double,double*,char*,uint64_t);
typedef struct penta_fem_host_v1 {
    uint32_t struct_size,abi_version;void* context;
    int (*cancelled)(void*);
    int (*charge)(void*,uint64_t,char*,uint64_t);
} penta_fem_host_v1;
typedef struct penta_fem_request_v1 {
    uint32_t struct_size,order,visualization_refinement,reserved;
    uint64_t vertices,triangles,boundaries;
    const double* points;const uint64_t *faces,*boundary_tags; /* one-based IDs */
    const uint64_t* dirichlet_tags;uint64_t dirichlet_count; /* UINT64_MAX = all */
    penta_fem_field_v1 forcing,diffusion,reaction,dirichlet,exact;
    void *forcing_context,*diffusion_context,*reaction_context,*dirichlet_context,*exact_context;
    double diffusion_constant,reaction_constant,dirichlet_constant;
    double relative_tolerance,absolute_tolerance,max_seconds;
    uint64_t max_iterations,max_work,max_bytes;
} penta_fem_request_v1;
typedef struct penta_fem_result_v1 {
    uint32_t struct_size,converged,has_exact,reserved;
    uint64_t dofs,dof_capacity,visualization_vertices,visualization_triangles,vertex_capacity,triangle_capacity;
    double *dof_coordinates,*dof_values,*visualization_coordinates,*visualization_values;
    uint64_t* visualization_faces;
    uint64_t iterations,evaluations,work_units,estimated_bytes;
    double residual_norm,relative_residual,l2_error,min_diffusion,min_reaction;
} penta_fem_result_v1;
typedef struct penta_fem_api_v1 {
    uint32_t struct_size,abi_version;
    int (*solve)(const penta_fem_host_v1*,const penta_fem_request_v1*,penta_fem_result_v1*,char*,uint64_t);
} penta_fem_api_v1;
typedef const penta_fem_api_v1* (*penta_fem_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_fem_api_v1* penta_fem_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
