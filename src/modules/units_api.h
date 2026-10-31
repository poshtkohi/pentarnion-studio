#pragma once
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_UNITS_ABI_V1 0x00010000u
/* Fixed exponent order: length(m), mass(kg), time(s), current(A),
 * temperature(K), amount(mol), luminous intensity(cd). Linear units only. */
typedef struct penta_si_unit_v1 {int32_t dimensions[7];double scale;char canonical[256];}penta_si_unit_v1;
enum penta_unit_operation_v1 {PENTA_UNIT_CONVERT=0,PENTA_UNIT_ADD,PENTA_UNIT_SUBTRACT,PENTA_UNIT_MULTIPLY,PENTA_UNIT_DIVIDE,PENTA_UNIT_POWER};
typedef struct penta_units_host_v1 {uint32_t struct_size,abi_version;void* context;int(*cancelled)(void*);int(*charge)(void*,uint64_t,char*,uint64_t);int(*parallel_for)(void*,uint64_t,uint64_t,penta_numeric_range_task_v1,void*,char*,uint64_t);}penta_units_host_v1;
typedef struct penta_units_request_v1 {uint32_t struct_size,operation;int32_t exponent;uint32_t reserved;penta_si_unit_v1 a_unit,b_unit;uint64_t a_rows,a_columns,b_rows,b_columns;const double *a,*b;uint64_t max_work;}penta_units_request_v1;
typedef struct penta_units_result_v1 {uint32_t struct_size,reserved;penta_si_unit_v1 unit;uint64_t rows,columns,capacity,work_units;double* values;}penta_units_result_v1;
typedef struct penta_units_api_v1 {uint32_t struct_size,abi_version;int(*parse)(const char*,penta_si_unit_v1*,char*,uint64_t);int(*execute)(const penta_units_host_v1*,const penta_units_request_v1*,penta_units_result_v1*,char*,uint64_t);}penta_units_api_v1;
typedef const penta_units_api_v1*(*penta_units_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_units_api_v1*penta_units_query_v1(uint32_t);
/* Additive ABI: rational SI exponents, explicit absolute temperatures and
 * logarithmic levels. V1 layouts and entry points remain unchanged. */
#define PENTA_UNITS_ABI_V2 0x00020000u
enum penta_unit_kind_v2 {PENTA_UNIT_LINEAR=0,PENTA_UNIT_ABSOLUTE=1,PENTA_UNIT_LOGARITHMIC=2};
typedef struct penta_si_unit_v2 {int32_t numerator[7],denominator[7];double scale,offset,log_base,log_factor;uint32_t kind;char canonical[512];}penta_si_unit_v2;
typedef struct penta_units_request_v2 {uint32_t struct_size,operation;int32_t power_numerator,power_denominator;penta_si_unit_v2 a_unit,b_unit;uint64_t a_rows,a_columns,b_rows,b_columns;const double *a,*b;uint64_t max_work;}penta_units_request_v2;
typedef struct penta_units_result_v2 {uint32_t struct_size,reserved;penta_si_unit_v2 unit;uint64_t rows,columns,capacity,work_units;double* values;}penta_units_result_v2;
typedef struct penta_units_api_v2 {uint32_t struct_size,abi_version;int(*parse)(const char*,penta_si_unit_v2*,char*,uint64_t);int(*derive)(const penta_units_request_v2*,penta_si_unit_v2*,char*,uint64_t);int(*execute)(const penta_units_host_v1*,const penta_units_request_v2*,penta_units_result_v2*,char*,uint64_t);}penta_units_api_v2;
typedef const penta_units_api_v2*(*penta_units_query_function_v2)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_units_api_v2*penta_units_query_v2(uint32_t);
#ifdef __cplusplus
}
#endif
