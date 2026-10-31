#ifndef PENTA_MAPS_API_H
#define PENTA_MAPS_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_MAPS_ABI_V1 0x00010000u
#if defined(_WIN32) && defined(PENTA_MAPS_MODULE_BUILD)
#define PENTA_MAPS_EXPORT __declspec(dllexport)
#elif !defined(_WIN32)
#define PENTA_MAPS_EXPORT __attribute__((visibility("default")))
#else
#define PENTA_MAPS_EXPORT
#endif
enum penta_maps_status_v1 {PENTA_MAPS_OK=0,PENTA_MAPS_ARGUMENT=1,PENTA_MAPS_DOMAIN=2,
    PENTA_MAPS_CANCELLED=3,PENTA_MAPS_INTERNAL=4,PENTA_MAPS_VERSION=5,
    PENTA_MAPS_BUDGET=6,PENTA_MAPS_CALLBACK=7};
/* Values and optional output-major real-coordinate Jacobian are borrowed only
   during this synchronous invocation. No C++ object/exception crosses the ABI. */
typedef int (*penta_maps_evaluate_v1)(void*,const double*,uint64_t,double*,double*,char*,uint64_t);
typedef struct penta_maps_host_v1 {
    uint32_t struct_size,abi_version;
    void* context;
    int (*cancelled)(void*);
    int (*charge)(void*,uint64_t,char*,uint64_t);
} penta_maps_host_v1;
typedef struct penta_maps_request_v1 {
    uint32_t struct_size,final_jacobian;
    uint64_t dimension,iterations,transient,stride,max_evaluations;
    double maximum_wall_seconds,escape_radius;
    const double* initial;
    void* callback_context;
    penta_maps_evaluate_v1 evaluate;
} penta_maps_request_v1;
typedef struct penta_maps_result_v1 {
    uint32_t struct_size,escaped;
    uint64_t iterations,evaluations,jacobian_evaluations,samples,escape_iteration;
    char status[128],message[512];
    double* solution;
    uint64_t solution_capacity;
    double* states;
    uint64_t states_capacity;
    double* indices;
    uint64_t indices_capacity;
    double* jacobian;
    uint64_t jacobian_capacity;
} penta_maps_result_v1;
typedef struct penta_maps_api_v1 {
    uint32_t struct_size,abi_version;
    const char* module_name;
    const char* build_identity;
    uint64_t capabilities;
    int (*iterate)(const penta_maps_host_v1*,const penta_maps_request_v1*,penta_maps_result_v1*,char*,uint64_t);
} penta_maps_api_v1;
typedef const penta_maps_api_v1* (*penta_maps_query_function_v1)(uint32_t);
PENTA_MAPS_EXPORT const penta_maps_api_v1* penta_maps_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
