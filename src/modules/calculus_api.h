#ifndef PENTA_CALCULUS_API_H
#define PENTA_CALCULUS_API_H
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
enum penta_numeric_calculus_operation_v1 {
    PENTA_CALC_INTERP_LINEAR=1,PENTA_CALC_INTERP_HERMITE,
    PENTA_CALC_TRAPZ,PENTA_CALC_CUMTRAPZ,
    PENTA_CALC_DERIVATIVE1,PENTA_CALC_DERIVATIVE2,PENTA_CALC_DIFF,PENTA_CALC_QUADRATURE
};
typedef int (*penta_numeric_integrand_v1)(void*,double,double*,uint32_t,char*,uint64_t);
/* Row-major numeric entries with 1 real or 5 Pentarnion coefficients.
 * value_stride permits selecting one state from sample-major ODE storage.
 * coordinates describes the selected axis, strictly increasing or decreasing.
 * Interpolation takes a column of samples (columns=1), output follows query order.
 * All output storage belongs to the caller; callbacks and pointers are borrowed
 * only for the duration of this synchronous call. */
typedef struct penta_numeric_calculus_request_v1 {
    uint32_t struct_size,operation,components,axis;
    uint64_t rows,columns,value_stride,data_capacity,derivative_capacity;
    const double* data;
    const double* derivative_data;
    const double* coordinates;
    uint64_t coordinate_count;
    const double* query;
    uint64_t query_count;
    uint32_t difference_order,extrapolate;
    double* output;
    uint64_t output_capacity;
    penta_numeric_integrand_v1 integrand;
    void* integrand_context;
    double absolute_tolerance,relative_tolerance;
    uint64_t maximum_intervals;
} penta_numeric_calculus_request_v1;
int penta_numeric_calculus_v1(const penta_numeric_host_v1*,
    const penta_numeric_calculus_request_v1*,char*,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
