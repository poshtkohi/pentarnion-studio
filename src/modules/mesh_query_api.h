#pragma once
#include "geometry_extra_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_MESH_QUERY_ABI_V1 0x10000u
enum {PENTA_MESH_CLOSEST=1,PENTA_MESH_PROBE=2,PENTA_MESH_CONTAINS=3,PENTA_MESH_RAY=4,PENTA_MESH_SLICE=5};
typedef struct penta_mesh_query_request_v1 {
    uint32_t struct_size,operation,field_width,reserved;
    penta_surface_mesh_v1 mesh;
    uint64_t count,direction_count,max_work,max_bytes;
    const double *queries,*directions,*field,*normal;
    double offset,tolerance,minimum_parameter,maximum_parameter,max_seconds;
} penta_mesh_query_request_v1;
typedef struct penta_mesh_query_result_v1 {
    uint32_t struct_size,reserved;
    uint64_t count,segments,capacity,segment_capacity,work_units,coplanar_faces;
    double *points,*values,*distances,*parameters,*barycentric;
    uint64_t *faces,*edges;
    int32_t* classification; /* closest/probe: within tolerance; contains:0/1/2(boundary)/-1(ambiguous); ray:0/1/2(coplanar ambiguity) */
} penta_mesh_query_result_v1;
typedef struct penta_mesh_query_api_v1 {
    uint32_t struct_size,abi_version;
    int (*compute)(const penta_geometry_extra_host_v1*,const penta_mesh_query_request_v1*,penta_mesh_query_result_v1*,char*,uint64_t);
} penta_mesh_query_api_v1;
typedef const penta_mesh_query_api_v1* (*penta_mesh_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_mesh_query_api_v1* penta_mesh_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
