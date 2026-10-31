#pragma once
#include "geometry_extra_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_MESH_INTERSECTION_ABI_V1 0x10000u
typedef struct penta_mesh_intersection_request_v1 {
    uint32_t struct_size,first_width,second_width,reserved;
    penta_surface_mesh_v1 first,second;
    const double *first_values,*second_values;
    uint64_t max_primitives,max_work,max_bytes;
    double tolerance,max_seconds;
} penta_mesh_intersection_request_v1;
typedef struct penta_mesh_intersection_result_v1 {
    uint32_t struct_size,reserved;
    uint64_t primitives,points,primitive_capacity,point_capacity,work_units,coplanar_pairs;
    double *coordinates,*first_barycentric,*second_barycentric,*first_values,*second_values;
    uint64_t *offsets,*counts,*first_faces,*second_faces;
    int32_t *dimensions,*coplanar;
} penta_mesh_intersection_result_v1;
typedef struct penta_mesh_intersection_api_v1 {
    uint32_t struct_size,abi_version;
    int (*compute)(const penta_geometry_extra_host_v1*,const penta_mesh_intersection_request_v1*,penta_mesh_intersection_result_v1*,char*,uint64_t);
} penta_mesh_intersection_api_v1;
typedef const penta_mesh_intersection_api_v1* (*penta_mesh_intersection_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_mesh_intersection_api_v1* penta_mesh_intersection_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
