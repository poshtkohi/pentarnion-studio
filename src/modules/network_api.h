#ifndef PENTA_NETWORK_API_H
#define PENTA_NETWORK_API_H
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_NETWORK_ABI_V1 0x00010000u
typedef struct penta_network_host_v1 {
    uint32_t struct_size,abi_version;
    void* context;
    int (*cancelled)(void*);
    int (*charge)(void*,uint64_t,char*,uint64_t);
    int (*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);
    int (*local)(void*,double,const double*,double*,double*,char*,uint64_t); /* Nullable row-major local Jacobian. */
} penta_network_host_v1;
typedef struct penta_network_request_v1 {
    uint32_t struct_size,method,directed,general_coupling;
    uint64_t nodes,edges,coordinates,samples;
    const uint64_t* endpoints; /* Source -> target, one-based physical edges. */
    const double* weights;
    const double* diffusion; /* coordinates real entries; node-independent. */
    const double* initial; /* node-major, coordinate-fastest. */
    const double* times;
    double step,max_seconds;
    uint64_t max_steps,max_work;
    double rtol,atol;
    uint64_t krylov_dimension;
} penta_network_request_v1;
typedef struct penta_network_result_v1 {
    uint32_t struct_size;
    double* values; /* sample,node,coordinate */
    double* means; /* sample,coordinate; Euclidean coordinate mean. */
    double* synchronization; /* sqrt(mean_node ||node-mean||_2^2). */
    uint64_t value_capacity,mean_capacity,sample_capacity;
    uint64_t steps,rhs_evaluations,work_units;
    double largest_step,elapsed_seconds;
    double last_step;
    uint64_t local_jacobian_evaluations,jacobian_vector_products,block_factorizations;
    uint64_t linear_iterations,linear_convergence_failures,nonlinear_iterations;
    uint64_t error_test_failures,preconditioner_setups,backend_rhs_evaluations;
} penta_network_result_v1;
typedef struct penta_network_api_v1 {
    uint32_t struct_size,abi_version;
    const char* module_name;
    int (*solve)(const penta_network_host_v1*,const penta_network_request_v1*,penta_network_result_v1*,char*,uint64_t);
} penta_network_api_v1;
typedef const penta_network_api_v1* (*penta_network_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_network_api_v1* penta_network_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
