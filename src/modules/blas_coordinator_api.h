#pragma once
#include <stdint.h>
#if defined(_WIN32) && defined(PENTA_BLAS_COORDINATOR_BUILD)
#define PENTA_BLAS_COORDINATOR_EXPORT __declspec(dllexport)
#elif defined(_WIN32)
#define PENTA_BLAS_COORDINATOR_EXPORT __declspec(dllimport)
#else
#define PENTA_BLAS_COORDINATOR_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_BLAS_COORDINATOR_ABI_V1 0x10000u
enum {PENTA_BLAS_EXCLUSIVE=1u};
typedef struct penta_blas_host_v1 {
    uint32_t struct_size,abi_version,granted_threads,reserved;
    void* context;int (*cancelled)(void*);
} penta_blas_host_v1;
typedef struct penta_blas_request_v1 {uint32_t struct_size,threads,flags,reserved;} penta_blas_request_v1;
typedef struct penta_blas_lease_v1 {uint32_t struct_size,threads,flags,reserved;uint64_t token;} penta_blas_lease_v1;
typedef struct penta_blas_status_v1 {
    uint32_t struct_size,backend_parallel,backend_threads,configured_threads;
    uint32_t active_leases,active_owners,waiting_leases,exclusive;
    uint64_t epochs,configuration_changes,compatible_overlaps,maximum_active_owners,wait_nanoseconds;
} penta_blas_status_v1;
typedef struct penta_blas_coordinator_api_v1 {
    uint32_t struct_size,abi_version;
    int (*begin)(const penta_blas_host_v1*,const penta_blas_request_v1*,penta_blas_lease_v1*,char*,uint64_t);
    int (*reassert)(const penta_blas_lease_v1*,char*,uint64_t);
    int (*downgrade)(penta_blas_lease_v1*,char*,uint64_t);
    int (*end)(penta_blas_lease_v1*,char*,uint64_t);
    int (*status)(penta_blas_status_v1*,char*,uint64_t);
} penta_blas_coordinator_api_v1;
typedef const penta_blas_coordinator_api_v1* (*penta_blas_coordinator_query_function_v1)(uint32_t);
PENTA_BLAS_COORDINATOR_EXPORT const penta_blas_coordinator_api_v1* penta_blas_coordinator_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
