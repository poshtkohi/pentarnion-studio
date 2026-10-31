#pragma once
#include "geometry_extra_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_INTRINSIC_ABI_V1 0x10000u
/* Metric/connection/tensor derivatives are output-major, then coordinate-major.
   Convention: nabla_(partial_j) partial_k=Gamma^i_jk partial_i and
   R^i_jkl partial_i=R(partial_k,partial_l)partial_j. */
typedef struct penta_intrinsic_request_v1 {
    uint32_t struct_size,dimension,tensor_rank,reserved;
    const double *metric,*metric_first,*metric_second;
    const double *connection,*connection_first;
    const double *tensor,*tensor_first;
    const int32_t* variance; /* +1 contravariant,-1 covariant per tensor slot */
    const double *frame,*frame_first; /* columns are moving frame vectors */
    uint64_t max_work;
    double tolerance;
} penta_intrinsic_request_v1;
typedef struct penta_intrinsic_result_v1 {
    uint32_t struct_size,dimension,levi_civita,reserved;
    uint64_t tensor_capacity,work_units;
    double *inverse_metric,*connection,*connection_first,*riemann,*ricci,*torsion,*metric_covariant,*tensor_covariant;
    double *frame_connection,*frame_bracket,*frame_metric;
    double scalar_curvature,volume_density,condition,torsion_norm,metric_covariant_norm;
} penta_intrinsic_result_v1;
typedef struct penta_chart_transition_request_v1 {
    uint32_t struct_size,dimension,reserved0,reserved1;
    const double *jacobian,*second,*metric,*connection,*vector,*covector;
    double tolerance;
} penta_chart_transition_request_v1;
typedef struct penta_chart_transition_result_v1 {
    uint32_t struct_size,reserved;
    double *inverse_jacobian,*metric,*connection,*vector,*covector;
    double determinant,condition;
} penta_chart_transition_result_v1;
typedef struct penta_intrinsic_api_v1 {
    uint32_t struct_size,abi_version;
    int (*point)(const penta_geometry_extra_host_v1*,const penta_intrinsic_request_v1*,penta_intrinsic_result_v1*,char*,uint64_t);
    int (*transition)(const penta_chart_transition_request_v1*,penta_chart_transition_result_v1*,char*,uint64_t);
} penta_intrinsic_api_v1;
typedef const penta_intrinsic_api_v1* (*penta_intrinsic_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_intrinsic_api_v1* penta_intrinsic_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
