#ifndef PENTA_NETWORK_OPERATOR_API_H
#define PENTA_NETWORK_OPERATOR_API_H
#include "network_api.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Additive interface: original real-diagonal network ABI remains unchanged.
   Each physical edge owns a row-major n-by-n real operator. The real graph
   weight and coordinate diffusion scale still multiply its ordered action. */
#define PENTA_NETWORK_OPERATOR_ABI_V1 0x00010000u
typedef struct penta_network_operator_api_v1 {
    uint32_t struct_size,abi_version;
    const char* module_name;
    int (*solve)(const penta_network_host_v1*,const penta_network_request_v1*,
                 const double*,uint64_t,penta_network_result_v1*,char*,uint64_t);
} penta_network_operator_api_v1;
typedef const penta_network_operator_api_v1* (*penta_network_operator_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_network_operator_api_v1* penta_network_operator_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
