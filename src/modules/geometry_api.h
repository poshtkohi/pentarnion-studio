#pragma once
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_GEOMETRY_ABI_V1 0x00010000u
/* Flattened derivatives are output-major: dX[mu*2+a], ddX[mu*4+a*2+b].
 * Metric outputs E,F,G use the same derivative order. Embedding coordinates
 * retain their declared real order, including (1,i,j,k,ell) in dimension five. */
typedef struct penta_chart_sample_v1 {
    uint32_t struct_size,dimension,custom_metric,reserved;
    double x[5],first[10],second[20];
    double metric[3],metric_first[6],metric_second[12];
} penta_chart_sample_v1;
typedef struct penta_chart_point_v1 {
    uint32_t struct_size,dimension,extrinsic_available,reserved;
    double x[5],tangent[10],metric[4],inverse_metric[4],christoffel[8];
    double riemann[16],ricci[4],gaussian_curvature,scalar_curvature,area_density;
    double second_fundamental[20],mean_curvature_vector[5],tangent_projector[25],normal_projector[25];
} penta_chart_point_v1;
typedef int (*penta_chart_evaluate_v1)(void*,double,double,penta_chart_sample_v1*,char*,uint64_t);
typedef struct penta_geometry_host_v1 {
    uint32_t struct_size,abi_version;void* context;
    int (*cancelled)(void*);int (*charge)(void*,uint64_t,char*,uint64_t);
} penta_geometry_host_v1;
typedef struct penta_geodesic_request_v1 {
    uint32_t struct_size,dimension,transport,reserved;
    double initial[2],velocity[2],transport_initial[2];
    double t0,t1,time_step,sample_step,metric_tolerance,max_wall_seconds;
    double lower[2],upper[2];uint64_t max_steps;
    void* callback_context;penta_chart_evaluate_v1 evaluate;
} penta_geodesic_request_v1;
typedef struct penta_geodesic_result_v1 {
    uint32_t struct_size,reserved;uint64_t capacity,samples,steps,evaluations;
    double *times,*coordinates,*velocity,*positions,*transport,*speed_squared,*transport_norm_squared;
    double elapsed_seconds,max_relative_speed_drift,max_relative_transport_drift;
} penta_geodesic_result_v1;
typedef struct penta_geometry_api_v1 {
    uint32_t struct_size,abi_version;
    int (*point)(const penta_chart_sample_v1*,double,penta_chart_point_v1*,char*,uint64_t);
    int (*geodesic)(const penta_geometry_host_v1*,const penta_geodesic_request_v1*,penta_geodesic_result_v1*,char*,uint64_t);
} penta_geometry_api_v1;
typedef const penta_geometry_api_v1* (*penta_geometry_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_geometry_api_v1* penta_geometry_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
