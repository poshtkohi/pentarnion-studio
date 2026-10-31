#ifndef PENTA_LINALG_API_H
#define PENTA_LINALG_API_H
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_LINALG_ABI_V1 0x00010000u
#if defined(_WIN32) && defined(PENTA_LINALG_MODULE_BUILD)
#define PENTA_LINALG_EXPORT __declspec(dllexport)
#elif !defined(_WIN32)
#define PENTA_LINALG_EXPORT __attribute__((visibility("default")))
#else
#define PENTA_LINALG_EXPORT
#endif
enum penta_linalg_operation_v1 {
    PENTA_LA_MULTIPLY=1,PENTA_LA_TRANSPOSE,PENTA_LA_REDUCE,
    PENTA_LA_LEFT_OPERATOR,PENTA_LA_RIGHT_OPERATOR,
    PENTA_LA_SOLVE=10,PENTA_LA_INVERSE,PENTA_LA_DETERMINANT,
    PENTA_LA_LU,PENTA_LA_QR,PENTA_LA_CHOLESKY,PENTA_LA_SVD,
    PENTA_LA_EIGEN,PENTA_LA_SCHUR,PENTA_LA_RANK,PENTA_LA_RCOND,PENTA_LA_COND,
    /* Additive capability0x80: value/factor-only variants omit unused rank and
       rcond metadata (those result fields remain0/unavailable). Original opcodes
       retain their full SVD-based2-norm diagnostics. No structure/ABI layout changes. */
    PENTA_LA_DETERMINANT_VALUE=22,PENTA_LA_CHOLESKY_FACTOR=23,
    PENTA_LA_SPARSE_MULTIPLY=30,PENTA_LA_SPARSE_SOLVE,PENTA_LA_SPARSE_TRANSPOSE
};
#define PENTA_LINALG_CAP_VALUE_ONLY 0x80u
enum penta_linalg_method_v1 {PENTA_LA_METHOD_LU=0,PENTA_LA_METHOD_QR=1,PENTA_LA_METHOD_CHOLESKY=2,PENTA_LA_METHOD_SVD=3,PENTA_LA_METHOD_SPARSELU=4,PENTA_LA_METHOD_BICGSTAB=5};
enum penta_linalg_reduce_v1 {PENTA_LA_SUM=1,PENTA_LA_MEAN,PENTA_LA_MIN,PENTA_LA_MAX};
enum penta_linalg_preconditioner_v1 {PENTA_LA_ILUT=0,PENTA_LA_DIAGONAL=1,PENTA_LA_IDENTITY=2};
typedef struct penta_linalg_matrix_v1 {
    uint64_t rows,columns,count; /* row-major scalar count, including batches */
    uint32_t width,reserved; /* real=1, Pentarnion=5 */
    const double* values;
} penta_linalg_matrix_v1;
typedef struct penta_linalg_sparse_v1 {
    uint64_t rows,columns,nonzeros;
    uint32_t width,reserved;
    const uint64_t* row_indices; /* zero-based canonical CSC */
    const uint64_t* column_pointers;
    const double* values;
    uint64_t index_capacity,pointer_capacity,value_capacity;
} penta_linalg_sparse_v1;
typedef struct penta_linalg_output_v1 {
    double* values;
    uint64_t capacity,rows,columns;
    uint32_t width,reserved;
} penta_linalg_output_v1;
typedef struct penta_linalg_sparse_output_v1 {
    uint64_t* row_indices;
    uint64_t* column_pointers;
    double* values;
    uint64_t index_capacity,pointer_capacity,value_capacity;
    uint64_t rows,columns,nonzeros;
    uint32_t width,reserved;
} penta_linalg_sparse_output_v1;
typedef struct penta_linalg_request_v1 {
    uint32_t struct_size,operation,method,threads;
    penta_linalg_matrix_v1 a,b;
    penta_linalg_sparse_v1 sparse_a,sparse_b;
    uint64_t batches; /* uniform dense product batches, otherwise1 */
    double gamma,rank_tolerance,rcond_min,residual_tolerance;
    uint32_t axis,reduce_operation,conjugate,preconditioner;
    double iterative_tolerance,drop_tolerance;
    uint64_t max_iterations,fill_factor;
} penta_linalg_request_v1;
typedef struct penta_linalg_result_v1 {
    uint32_t struct_size,converged,backend_threads,reserved;
    penta_linalg_output_v1 output[4];
    penta_linalg_sparse_output_v1 sparse;
    double scalar,residual_norm,relative_residual,rcond,error_estimate;
    uint64_t rank,iterations;
} penta_linalg_result_v1;
typedef struct penta_linalg_api_v1 {
    uint32_t struct_size,abi_version;
    const char* module_name;
    const char* build_identity;
    uint64_t capabilities;
    int (*compute)(const penta_numeric_host_v1*,const penta_linalg_request_v1*,penta_linalg_result_v1*,char*,uint64_t);
} penta_linalg_api_v1;
typedef const penta_linalg_api_v1* (*penta_linalg_query_function_v1)(uint32_t);
PENTA_LINALG_EXPORT const penta_linalg_api_v1* penta_linalg_query_v1(uint32_t requested_abi);
#ifdef __cplusplus
}
#endif
#endif
