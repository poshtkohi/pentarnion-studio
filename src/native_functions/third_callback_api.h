#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_THIRD_CALLBACK_ABI_V1 65536u
/* Optional capability of AD3 generated modules. AD1/AD2 exports are unchanged.
   All counts and coordinates are real, including every Pentarnion coefficient.
   Input/seed/dependency arrays have inputs entries; outputs have outputs entries.
   Host and arrays live for the synchronous call only. */
typedef struct penta_third_host_v1 {
    uint32_t struct_size,abi_version;
    void* context;
    int (*charge)(void*,uint64_t,char*,uint64_t);
} penta_third_host_v1;
typedef int (*penta_third_entry_v1)(double,const double*,const double*,const double*,const double*,
    const unsigned char*,double*,double*,const penta_third_host_v1*,char*,uint64_t);
typedef struct penta_third_api_v1 {
    uint32_t struct_size,abi_version,inputs,outputs;
    penta_third_entry_v1 evaluate;
} penta_third_api_v1;
typedef const penta_third_api_v1* (*penta_third_query_v1)(uint32_t);
#ifdef __cplusplus
}
#endif
