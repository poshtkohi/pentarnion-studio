#pragma once
#include "pde_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_PDE_CAP_IMEX_EULER_V1 UINT64_C(2)
enum penta_pde_integrator_v1 { PENTA_PDE_RK4_V1=0, PENTA_PDE_IMEX_EULER_V1=1 };
typedef struct penta_pde_time_request_v1 {
    penta_pde_request_v1 base;
    uint32_t integrator;
} penta_pde_time_request_v1;
typedef struct penta_pde_time_result_v1 {
    penta_pde_result_v1 base;
    uint64_t linear_solves,factorizations;
} penta_pde_time_result_v1;
#ifdef __cplusplus
}
#endif
