#ifndef PENTA_GEOMETRY_EXTRA_API_H
#define PENTA_GEOMETRY_EXTRA_API_H
#include "geometry_api.h"
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_GEOMETRY_EXTRA_ABI_V1 0x00010000u
typedef struct penta_geometry_extra_host_v1 {
 uint32_t struct_size,abi_version;void* context;
 int (*cancelled)(void*);int (*charge)(void*,uint64_t,char*,uint64_t);
 int (*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
} penta_geometry_extra_host_v1;
typedef struct penta_surface_mesh_v1 {
 uint32_t dimension,reserved;uint64_t vertices,triangles,boundaries;
 const double* points;const uint64_t *faces,*boundary_tags;
 double gamma; /* reserved=1 means all five Pentarnion coefficient coordinates. */
} penta_surface_mesh_v1;
enum penta_mesh_operation_v1 {PENTA_MESH_PLANE=1,PENTA_MESH_SPHERE,PENTA_MESH_CYLINDER,PENTA_MESH_CIRCLE,PENTA_MESH_PARAMETRIC,PENTA_MESH_COPY,PENTA_MESH_TRANSFORM,PENTA_MESH_REFINE,PENTA_MESH_INFO,PENTA_MESH_LINE_PLANE,PENTA_MESH_RAY_SPHERE,PENTA_MESH_READ,PENTA_MESH_WRITE};
typedef int (*penta_embedding_value_v1)(void*,double,double,double*,uint32_t,char*,uint64_t);
typedef struct penta_mesh_request_v1 {
 uint32_t struct_size,operation,dimension,flags;penta_surface_mesh_v1 mesh;
 uint64_t sizes[4];double parameters[24];
 const double *u,*v,*matrix,*translation;uint64_t nu,nv;
 void* callback_context;penta_embedding_value_v1 embedding;
 const char* path;uint64_t max_work;
} penta_mesh_request_v1;
typedef struct penta_mesh_result_v1 {
 uint32_t struct_size,dimension,closed,orientation_consistent,volume_available,status;
 uint32_t coordinate_kind,reserved;double gamma;
 uint64_t vertices,triangles,boundaries,point_capacity,face_capacity,boundary_capacity,normal_capacity,projector_capacity,area_capacity,work_units;
 double *points,*normals,*normal_projectors,*face_areas;uint64_t *faces,*boundary_tags;
 double area,length,volume,min_angle,max_aspect,parameters[2];
} penta_mesh_result_v1;
enum penta_form_operation_v1 {PENTA_FORM_COPY=1,PENTA_FORM_WEDGE,PENTA_FORM_D,PENTA_FORM_HODGE};
typedef struct penta_form_value_v1 {
 uint32_t dimension,degree,derivative_order,constant_coefficients;int32_t orientation;uint32_t reserved;
 const double *coefficients,*first,*second,*metric;
} penta_form_value_v1;
typedef struct penta_form_request_v1 {uint32_t struct_size,operation;penta_form_value_v1 a,b;uint64_t max_work;double tolerance;} penta_form_request_v1;
typedef struct penta_form_result_v1 {uint32_t struct_size,dimension,degree,derivative_order,constant_coefficients,reserved;uint64_t capacity,count,work_units;double *coefficients,*first,*second;} penta_form_result_v1;
typedef int (*penta_surface_field_v1)(void*,double,const double*,uint32_t,const double*,uint32_t,double*,uint32_t,char*,uint64_t);
typedef struct penta_surface_heat_request_v1 {
 uint32_t struct_size,width,dirichlet_count,reserved;penta_surface_mesh_v1 mesh;
 const double* initial;const uint64_t* dirichlet_tags;const double* dirichlet_values;
 double t0,t1,time_step,sample_step,diffusion,max_wall_seconds,tolerance;uint64_t max_steps,max_work;
 void *reaction_context,*diffusion_context;
 penta_surface_field_v1 reaction,diffusion_field;
 void* initial_context;penta_surface_field_v1 initial_field;
} penta_surface_heat_request_v1;
typedef struct penta_surface_heat_result_v1 {
 uint32_t struct_size,reserved;uint64_t samples,capacity,steps,factorizations,evaluations,work_units;
 double *times,*values,*means,*energy;double max_mass_drift[5],max_energy_increase;
} penta_surface_heat_result_v1;
typedef struct penta_geometry_extra_api_v1 {
 uint32_t struct_size,abi_version;
 int (*mesh)(const penta_geometry_extra_host_v1*,const penta_mesh_request_v1*,penta_mesh_result_v1*,char*,uint64_t);
 int (*forms)(const penta_geometry_extra_host_v1*,const penta_form_request_v1*,penta_form_result_v1*,char*,uint64_t);
 int (*laplace)(const penta_chart_point_v1*,const double*,const double*,double*,char*,uint64_t);
 int (*heat)(const penta_geometry_extra_host_v1*,const penta_surface_heat_request_v1*,penta_surface_heat_result_v1*,char*,uint64_t);
} penta_geometry_extra_api_v1;
typedef const penta_geometry_extra_api_v1* (*penta_geometry_extra_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_geometry_extra_api_v1* penta_geometry_extra_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
