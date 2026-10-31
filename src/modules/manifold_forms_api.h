#pragma once
#include "geometry_extra_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_MANIFOLD_FORMS_ABI_V1 0x10000u
enum {PENTA_FORM_CODIFFERENTIAL=5,PENTA_FORM_LAPLACIAN=6};
typedef struct penta_manifold_form_request_v1 {
    penta_form_request_v1 base;
    const double *metric_first,*metric_second;
} penta_manifold_form_request_v1;
typedef struct penta_manifold_forms_api_v1 {
    uint32_t struct_size,abi_version;
    int (*compute)(const penta_geometry_extra_host_v1*,const penta_manifold_form_request_v1*,penta_form_result_v1*,char*,uint64_t);
} penta_manifold_forms_api_v1;
typedef const penta_manifold_forms_api_v1* (*penta_manifold_forms_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_manifold_forms_api_v1* penta_manifold_forms_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
