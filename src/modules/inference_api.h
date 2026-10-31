#pragma once
#include "fit_api.h"
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_INFERENCE_ABI_V1 0x00010000u
#define PENTA_INFERENCE_ABI_V2 0x00020000u
enum penta_inference_operation_v1{PENTA_INF_MULTISTART=0,PENTA_INF_PROFILE=1,PENTA_INF_BOOTSTRAP=2,PENTA_INF_COVERAGE=3};
enum penta_inference_task_status_v1{PENTA_INF_PENDING=0,PENTA_INF_CONVERGED=1,PENTA_INF_NOT_CONVERGED=2,PENTA_INF_FAILED=3,PENTA_INF_RANK_DEFICIENT=4};
typedef int(*penta_inference_model_v1)(void*,const double*,uint64_t,double,double*,double*,char*,uint64_t);
/* Each native Ceres solve reports its real iteration stream. Baseline task_id=0,
 * independent catalogue rows use one-based IDs. Profile nuisance coordinates
 * are expanded back to the complete physical parameter vector by the host. */
typedef struct penta_inference_fit_metadata_v1{
    uint32_t struct_size,full_dimension;uint64_t task_id,free_count;
    const uint32_t* free_indices;const double* full_template;
}penta_inference_fit_metadata_v1;
#define PENTA_INFERENCE_STOPPED 8

typedef struct penta_inference_request_v1{
    uint32_t struct_size,operation,dimension,bootstrap_method,profile_coordinate,block_length;
    uint64_t observations,prediction_count,runs,seed,batch_size;
    const double *initial,*starts,*x,*y,*prediction_x,*profile_grid;
    double confidence,max_wall_seconds;
    penta_fit_request_v1 fit;
    void* model_context;penta_inference_model_v1 model;
}penta_inference_request_v1;
typedef struct penta_inference_result_v1{
    uint32_t struct_size,base_ready,statistics_valid,cancelled;
    uint64_t capacity,prediction_capacity,completed,successful,best_task,base_rank;
    double *parameters,*costs,*predictions,*base_parameters,*base_predictions;
    uint32_t* status;uint64_t *iterations,*evaluations,*ranks;char* messages;
    double *parameter_mean,*parameter_std,*correlation,*correlation_defined,*fitted_lower,*fitted_upper;
    double base_cost,elapsed_seconds;char base_message[512];
    uint32_t user_stopped;
}penta_inference_result_v1;
typedef struct penta_inference_host_v1{
    uint32_t struct_size,abi_version;void* context;
    int(*cancelled)(void*);
    int(*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
    int(*fit)(void*,const penta_fit_request_v1*,const penta_inference_fit_metadata_v1*,penta_fit_result_v1*,char*,uint64_t);
    int(*checkpoint)(void*,const penta_inference_result_v1*,char*,uint64_t);
    int(*stopped)(void*);
}penta_inference_host_v1;
typedef struct penta_inference_api_v1{
    uint32_t struct_size,abi_version;
    int(*run)(const penta_inference_host_v1*,const penta_inference_request_v1*,penta_inference_result_v1*,char*,uint64_t);
}penta_inference_api_v1;
typedef const penta_inference_api_v1*(*penta_inference_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_inference_api_v1*penta_inference_query_v1(uint32_t);
/* V1 structs/query remain byte-for-byte unchanged. The V2 entry point opts in
 * to request.fit.reserved noise tags0..3, bootstrap_method3 and operation3.
 * Older DLLs cannot silently reinterpret a new likelihood as ordinary SSE. */
typedef struct penta_inference_api_v2 {
    uint32_t struct_size,abi_version;
    int(*run)(const penta_inference_host_v1*,const penta_inference_request_v1*,penta_inference_result_v1*,char*,uint64_t);
}penta_inference_api_v2;
PENTA_NUMERIC_EXPORT const penta_inference_api_v2*penta_inference_query_v2(uint32_t);
#ifdef __cplusplus
}
#endif
