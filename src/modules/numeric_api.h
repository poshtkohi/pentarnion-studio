#ifndef PENTA_NUMERIC_API_H
#define PENTA_NUMERIC_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_NUMERIC_ABI_V1 0x00010000u
#if defined(_WIN32) && defined(PENTA_NUMERIC_MODULE_BUILD)
#define PENTA_NUMERIC_EXPORT __declspec(dllexport)
#elif !defined(_WIN32)
#define PENTA_NUMERIC_EXPORT __attribute__((visibility("default")))
#else
#define PENTA_NUMERIC_EXPORT
#endif
enum penta_numeric_status_v1 {
    PENTA_NUMERIC_OK=0, PENTA_NUMERIC_ARGUMENT=1, PENTA_NUMERIC_DOMAIN=2,
    PENTA_NUMERIC_CANCELLED=3, PENTA_NUMERIC_INTERNAL=4, PENTA_NUMERIC_VERSION=5,
    PENTA_NUMERIC_DIVISION=6
};
typedef int (*penta_numeric_range_task_v1)(void*,uint64_t,uint64_t,char*,uint64_t);
typedef struct penta_numeric_host_v1 {
    uint32_t struct_size,abi_version;
    void* context;
    int (*cancelled)(void*);
    int (*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
} penta_numeric_host_v1;
enum penta_numeric_vector_operation_v1 {
    PENTA_VEC_ADD=1,PENTA_VEC_SUBTRACT,PENTA_VEC_MULTIPLY,PENTA_VEC_DIVIDE,
    PENTA_VEC_POWER,PENTA_VEC_NEGATE,PENTA_VEC_AXPY,
    PENTA_VEC_SIN,PENTA_VEC_COS,PENTA_VEC_EXP,PENTA_VEC_SINH,PENTA_VEC_COSH,
    PENTA_VEC_SQRT,PENTA_VEC_TAN,PENTA_VEC_ASIN,PENTA_VEC_ACOS,PENTA_VEC_ATAN,
    PENTA_VEC_TANH,PENTA_VEC_ASINH,PENTA_VEC_ACOSH,PENTA_VEC_ATANH,
    PENTA_VEC_LOG,PENTA_VEC_LOG10,PENTA_VEC_LOG2,PENTA_VEC_ABS,
    PENTA_VEC_FLOOR,PENTA_VEC_CEIL,PENTA_VEC_ROUND,
    PENTA_VEC_CONJUGATE,PENTA_VEC_SCALAR_PART,PENTA_VEC_PURE_PART,PENTA_VEC_NORM2,
    PENTA_VEC_EXPM1,PENTA_VEC_LOG1P,PENTA_VEC_ERF,PENTA_VEC_ERFC,PENTA_VEC_SIGN
};
typedef struct penta_numeric_vector_request_v1 {
    uint32_t struct_size,operation;
    uint64_t count,a_count,b_count;
    uint32_t a_width,b_width,output_width,alpha_width;
    const double* a;
    const double* b;
    const double* alpha;
    double gamma;
    double* output;
    uint64_t output_capacity;
} penta_numeric_vector_request_v1;
enum penta_numeric_reduce_operation_v1 {
    PENTA_REDUCE_SUM=1,PENTA_REDUCE_MEAN,PENTA_REDUCE_DOT,
    PENTA_REDUCE_NORM,PENTA_REDUCE_NORM_SQUARED,PENTA_REDUCE_NORM_L1,PENTA_REDUCE_NORM_INFINITY
};
typedef struct penta_numeric_reduce_request_v1 {
    uint32_t struct_size,operation,width,reserved;
    uint64_t count;
    const double* a;
    const double* b;
    double* output;
    uint64_t output_capacity;
} penta_numeric_reduce_request_v1;
struct penta_numeric_calculus_request_v1;
typedef struct penta_numeric_api_v1 {
    uint32_t struct_size,abi_version;
    const char* module_name;
    const char* build_identity;
    uint64_t capabilities;
    int (*vector)(const penta_numeric_host_v1*,const penta_numeric_vector_request_v1*,char*,uint64_t);
    int (*reduce)(const penta_numeric_host_v1*,const penta_numeric_reduce_request_v1*,char*,uint64_t);
    int (*calculus)(const penta_numeric_host_v1*,const struct penta_numeric_calculus_request_v1*,char*,uint64_t);
} penta_numeric_api_v1;
typedef const penta_numeric_api_v1* (*penta_numeric_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_numeric_api_v1* penta_numeric_query_v1(uint32_t requested_abi);
#ifdef __cplusplus
}
#endif
#endif
