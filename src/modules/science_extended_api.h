#pragma once
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_SCIENCE_ABI_V2 0x00020000u
typedef int(*penta_statistic_v2)(void*,const double*,uint64_t,double*,char*,uint64_t);
typedef struct penta_science_host_v2 {
    uint32_t struct_size,abi_version;void* context;
    int(*cancelled)(void*);int(*charge)(void*,uint64_t,char*,uint64_t);
    int(*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
}penta_science_host_v2;
typedef struct penta_science_request_v2 {
    uint32_t struct_size,operation;
    const double *a,*b,*c,*d;uint64_t a_count,b_count,c_count,d_count;
    const char* options;uint64_t options_bytes;
    void* statistic_context;penta_statistic_v2 statistic;
    double* output;uint64_t output_capacity;
    char* metadata;uint64_t metadata_capacity;
}penta_science_request_v2;
enum penta_science_operation_v2 {PENTA_SCIENCE_FFT=1,PENTA_SCIENCE_WELCH,PENTA_SCIENCE_STFT,PENTA_SCIENCE_ISTFT,PENTA_SCIENCE_FILTER,PENTA_SCIENCE_FILTFILT,PENTA_SCIENCE_BUTTER,PENTA_SCIENCE_PEAKS,PENTA_SCIENCE_DISTRIBUTION,PENTA_SCIENCE_BOOTSTRAP,PENTA_SCIENCE_CROSS};
typedef struct penta_science_api_v2 {uint32_t struct_size,abi_version;uint64_t capabilities;int(*execute)(const penta_science_host_v2*,const penta_science_request_v2*,uint64_t*,char*,uint64_t);}penta_science_api_v2;
PENTA_NUMERIC_EXPORT const penta_science_api_v2*penta_science_query_v2(uint32_t);
#ifdef __cplusplus
}
#endif
