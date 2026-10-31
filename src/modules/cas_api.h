#ifndef PENTA_CAS_API_H
#define PENTA_CAS_API_H
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_CAS_ABI_V1 0x00010000u
typedef struct penta_cas_host_v1 {uint32_t struct_size,abi_version;void* context;int(*cancelled)(void*);int(*charge)(void*,uint64_t,char*,uint64_t);} penta_cas_host_v1;
typedef struct penta_cas_request_v1 {uint32_t struct_size;const char* input;uint64_t input_bytes,max_work;char* output;uint64_t output_capacity;} penta_cas_request_v1;
typedef struct penta_cas_api_v1 {uint32_t struct_size,abi_version;int(*execute)(const penta_cas_host_v1*,const penta_cas_request_v1*,char*,uint64_t);} penta_cas_api_v1;
typedef const penta_cas_api_v1*(*penta_cas_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_cas_api_v1* penta_cas_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
