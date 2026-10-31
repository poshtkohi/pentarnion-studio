#ifndef PENTA_CHAOS_API_H
#define PENTA_CHAOS_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_CHAOS_ABI_V1 0x00010000u
#if defined(_WIN32) && defined(PENTA_CHAOS_MODULE_BUILD)
#define PENTA_CHAOS_EXPORT __declspec(dllexport)
#elif !defined(_WIN32)
#define PENTA_CHAOS_EXPORT __attribute__((visibility("default")))
#else
#define PENTA_CHAOS_EXPORT
#endif
enum penta_chaos_status_v1 {PENTA_CHAOS_OK=0,PENTA_CHAOS_ARGUMENT=1,
    PENTA_CHAOS_DOMAIN=2,PENTA_CHAOS_INTERNAL=3,PENTA_CHAOS_VERSION=4};
typedef struct penta_chaos_qr_v1 {
    uint32_t struct_size,reserved;
    uint64_t dimension;
    const double* tangent; /* row-major n by n */
    double* basis; /* row-major n by n */
    uint64_t basis_capacity;
    double* logarithms; /* log(abs(R_jj)), n entries */
    uint64_t logarithms_capacity;
    double orthogonality_error;
} penta_chaos_qr_v1;
typedef struct penta_chaos_recurrence_v1 {
    uint32_t struct_size,reserved;
    uint64_t samples,dimension,theiler;
    const double* states; /* sample-major real coordinates */
    double radius;
    double* matrix; /* sample by sample, excluded band is zero */
    uint64_t matrix_capacity;
    uint64_t eligible,recurrent;
} penta_chaos_recurrence_v1;
typedef struct penta_chaos_api_v1 {
    uint32_t struct_size,abi_version;
    const char* module_name;
    const char* build_identity;
    uint64_t capabilities;
    int (*orthonormalize)(penta_chaos_qr_v1*,char*,uint64_t);
    int (*recurrence)(penta_chaos_recurrence_v1*,char*,uint64_t);
} penta_chaos_api_v1;
typedef const penta_chaos_api_v1* (*penta_chaos_query_function_v1)(uint32_t);
PENTA_CHAOS_EXPORT const penta_chaos_api_v1* penta_chaos_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
