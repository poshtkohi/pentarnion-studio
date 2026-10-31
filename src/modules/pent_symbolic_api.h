#ifndef PENTA_PENT_SYMBOLIC_API_H
#define PENTA_PENT_SYMBOLIC_API_H
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_PENT_SYMBOLIC_ABI_V1 0x00010000u
enum penta_pent_symbolic_operation_v1 {PENTA_PENT_EXPAND=1,PENTA_PENT_DIFF,PENTA_PENT_SUBS,PENTA_PENT_NUMERIC,PENTA_PENT_CODE};
enum penta_pent_symbolic_error_v1 {PENTA_PENT_SYMBOLIC_LIMIT=7};
typedef struct penta_pent_symbolic_host_v1 {
 uint32_t struct_size,abi_version;void* context;
 int (*cancelled)(void*);int (*charge)(void*,uint64_t,char*,uint64_t);
} penta_pent_symbolic_host_v1;
typedef struct penta_pent_symbolic_request_v1 {
 uint32_t struct_size,operation;
 const char *expression,*real_symbols,*pent_symbols,*gamma,*variable,*replacement;
 uint64_t max_work,binding_count;
 const char* const* binding_names;const double* binding_values;
 char* output;uint64_t output_capacity;
} penta_pent_symbolic_request_v1;
typedef struct penta_pent_symbolic_report_v1 {
 uint32_t struct_size,reserved;double coefficients[5],gamma;uint64_t work_units;
} penta_pent_symbolic_report_v1;
typedef struct penta_pent_symbolic_api_v1 {
 uint32_t struct_size,abi_version;
 const char* capability;
 int (*execute)(const penta_pent_symbolic_host_v1*,const penta_pent_symbolic_request_v1*,penta_pent_symbolic_report_v1*,char*,uint64_t);
} penta_pent_symbolic_api_v1;
typedef const penta_pent_symbolic_api_v1* (*penta_pent_symbolic_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_pent_symbolic_api_v1* penta_science_pent_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
