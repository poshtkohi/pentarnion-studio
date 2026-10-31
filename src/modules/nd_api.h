#ifndef PENTA_ND_API_H
#define PENTA_ND_API_H
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_ND_ABI_V1 0x00010000u
enum penta_nd_kind_v1 {PENTA_ND_REAL=1,PENTA_ND_COMPLEX=2,PENTA_ND_PENTARNION=5};
enum penta_nd_operation_v1 {PENTA_ND_COPY=1,PENTA_ND_PERMUTE,PENTA_ND_SLICE,PENTA_ND_ADD,PENTA_ND_SUBTRACT,PENTA_ND_MULTIPLY,PENTA_ND_DIVIDE,PENTA_ND_MATMUL,PENTA_ND_CONJUGATE,PENTA_ND_ABS,PENTA_ND_REAL_PART,PENTA_ND_IMAG_PART,PENTA_ND_COEFFICIENT,PENTA_ND_FFT,PENTA_ND_IFFT,PENTA_ND_GET,PENTA_ND_SET,PENTA_ND_MAP,PENTA_ND_INTERPOLATE,PENTA_ND_SOLVE,PENTA_ND_INVERSE,PENTA_ND_LU};
enum penta_nd_error_v1 {PENTA_ND_WORK_LIMIT=7};
typedef struct penta_nd_array_v1 {uint32_t rank,kind;uint64_t shape[8],data_capacity;const double* data;double gamma;} penta_nd_array_v1;
typedef struct penta_nd_host_v1 {
 uint32_t struct_size,abi_version;void* context;
 int (*cancelled)(void*);int (*charge)(void*,uint64_t,char*,uint64_t);
 int (*parallel_for)(void*,uint64_t,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
 int (*map)(void*,const double*,uint32_t,double*,uint32_t,char*,uint64_t);
} penta_nd_host_v1;
typedef struct penta_nd_request_v1 {
 uint32_t struct_size,operation,axis,index_count;
 penta_nd_array_v1 a,b;
 uint64_t order[8],starts[8],stops[8],steps[8],indices[8];
 uint64_t max_work,scratch_bytes;
 const double *coordinates,*query;uint64_t query_count;
 double* output;uint64_t output_capacity;
} penta_nd_request_v1;
typedef struct penta_nd_report_v1 {uint32_t struct_size,rank,kind,reserved;uint64_t shape[8],output_count,work_units;double gamma;} penta_nd_report_v1;
typedef struct penta_nd_api_v1 {uint32_t struct_size,abi_version;const char* capability;int (*execute)(const penta_nd_host_v1*,const penta_nd_request_v1*,penta_nd_report_v1*,char*,uint64_t);} penta_nd_api_v1;
typedef const penta_nd_api_v1* (*penta_nd_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_nd_api_v1* penta_nd_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
