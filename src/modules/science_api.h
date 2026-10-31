#ifndef PENTA_SCIENCE_API_H
#define PENTA_SCIENCE_API_H
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_SCIENCE_ABI_V1 0x00010000u
enum penta_science_operation_v1 { PENTA_SCI_FFT=1,PENTA_SCI_IFFT,PENTA_SCI_WELCH,PENTA_SCI_AUTOCORR,PENTA_SCI_PEAKS,PENTA_SCI_VARIANCE,PENTA_SCI_COVARIANCE,PENTA_SCI_QUANTILE,PENTA_SCI_HISTOGRAM,PENTA_SCI_BOOTSTRAP,PENTA_SCI_NORMPDF,PENTA_SCI_NORMCDF,PENTA_SCI_NORMINV,PENTA_SCI_STDDEV };
/* VARIANCE/STDDEV share parameters[0]: 0 sample, nonzero population. STDDEV
   takes its square root before narrowing the centered wide accumulation. */
typedef struct penta_science_request_v1 {
 uint32_t struct_size,operation;
 uint64_t count,rows,columns,secondary_count,seed;
 const double *a,*b;
 double parameters[8];
 double* output;
 uint64_t output_capacity;
} penta_science_request_v1;
typedef struct penta_science_report_v1 { uint32_t struct_size,reserved;uint64_t count; } penta_science_report_v1;
enum penta_symbolic_operation_v1 { PENTA_SYM_PARSE=1,PENTA_SYM_DIFF,PENTA_SYM_SUBS,PENTA_SYM_SIMPLIFY,PENTA_SYM_NUMERIC };
typedef struct penta_symbolic_request_v1 {
 uint32_t struct_size,operation;
 const char *expression,*variable,*replacement;
 char* output;
 uint64_t output_capacity;
 double numeric_value;
} penta_symbolic_request_v1;
typedef struct penta_science_api_v1 {
 uint32_t struct_size,abi_version;
 const char *module_name,*build_identity;
 int (*numeric)(const penta_numeric_host_v1*,const penta_science_request_v1*,penta_science_report_v1*,char*,uint64_t);
 int (*symbolic)(const penta_numeric_host_v1*,const penta_symbolic_request_v1*,double*,char*,uint64_t);
} penta_science_api_v1;
typedef const penta_science_api_v1* (*penta_science_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_science_api_v1* penta_science_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
