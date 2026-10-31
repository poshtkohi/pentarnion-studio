#pragma once
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_DISCOVERY_ABI_V1 0x10000u
enum penta_discovery_status_v1 {
    PENTA_DISCOVERY_PENDING=0,PENTA_DISCOVERY_BOUNDED=1,
    PENTA_DISCOVERY_EQUILIBRIUM=2,PENTA_DISCOVERY_RECURRENT=3,
    PENTA_DISCOVERY_PERIODIC=4,PENTA_DISCOVERY_THRESHOLD=5,
    PENTA_DISCOVERY_FAILURE=6,PENTA_DISCOVERY_TIMEOUT=7,
    PENTA_DISCOVERY_INCONCLUSIVE=8,PENTA_DISCOVERY_CANCELLED=9
};
typedef struct penta_discovery_trajectory_v1 {
    uint32_t struct_size,threshold_reached;
    uint64_t samples,coordinates,steps,rhs_evaluations;
    const double *times,*values,*final_rhs;
    void* handle;
} penta_discovery_trajectory_v1;
typedef struct penta_discovery_request_v1 {
    uint32_t struct_size;
    uint64_t runs,coordinates,physical_coordinates,parameters;
    const double *initial,*parameter_values,*gamma;
    double t0,t1,transient,return_tolerance,equilibrium_tolerance,min_return_time;
    double maximum_wall_seconds;
    uint64_t batch_size;
} penta_discovery_request_v1;
typedef struct penta_discovery_result_v1 {
    uint32_t struct_size;
    uint64_t capacity,completed;
    uint32_t* status;
    double *states,*stop_times,*maximum_norm,*return_error,*period,*rhs_norm;
    uint64_t *steps,*evaluations;
    char* messages; /* capacity * 512 bytes, one bounded UTF-8 diagnostic per row */
    double elapsed_seconds;
} penta_discovery_result_v1;
typedef struct penta_discovery_host_v1 {
    uint32_t struct_size,abi_version;
    void* context;
    int (*cancelled)(void*);
    int (*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
    int (*solve)(void*,uint64_t,const double*,const double*,double,penta_discovery_trajectory_v1*,char*,uint64_t);
    void (*release)(void*,penta_discovery_trajectory_v1*);
    int (*checkpoint)(void*,const penta_discovery_result_v1*,char*,uint64_t);
} penta_discovery_host_v1;
typedef struct penta_discovery_api_v1 {
    uint32_t struct_size,abi_version;
    uint64_t capabilities;
    int (*run)(const penta_discovery_host_v1*,const penta_discovery_request_v1*,penta_discovery_result_v1*,char*,uint64_t);
} penta_discovery_api_v1;
typedef const penta_discovery_api_v1* (*penta_discovery_query_function_v1)(uint32_t);
#ifdef __cplusplus
}
#endif
