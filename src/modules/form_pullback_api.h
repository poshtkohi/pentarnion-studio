#pragma once
#include "geometry_extra_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_FORM_PULLBACK_ABI_V1 0x10000u
typedef struct penta_form_pullback_request_v1 {
    uint32_t struct_size,domain_dimension;
    penta_form_value_v1 source;
    const double *metric_first,*metric_second;
    /* Target-output major, then domain derivative indices, all dense. */
    const double *map_first,*map_second,*map_third;
    uint64_t max_work;
    double tolerance;
} penta_form_pullback_request_v1;
typedef struct penta_form_pullback_result_v1 {
    uint32_t struct_size,reserved;
    penta_form_result_v1 form;
    double *metric,*metric_first,*metric_second;
} penta_form_pullback_result_v1;
typedef struct penta_form_pullback_api_v1 {
    uint32_t struct_size,abi_version;
    int (*compute)(const penta_geometry_extra_host_v1*,const penta_form_pullback_request_v1*,penta_form_pullback_result_v1*,char*,uint64_t);
} penta_form_pullback_api_v1;
typedef const penta_form_pullback_api_v1* (*penta_form_pullback_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_form_pullback_api_v1* penta_form_pullback_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
