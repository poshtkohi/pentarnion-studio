#ifndef PENTA_ORBITS_API_H
#define PENTA_ORBITS_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_ORBITS_ABI_V1 0x00010000u
typedef struct penta_orbits_host_v1 {
    uint32_t struct_size,abi_version;void* context;
    int (*cancelled)(void*);int (*charge)(void*,uint64_t,char*,uint64_t);
} penta_orbits_host_v1;
/* All arrays are borrowed, caller-owned real-coordinate buffers. */
typedef int (*penta_orbits_flow_v1)(void*,const double*,double,int,double*,double*,double*,char*,uint64_t);
typedef struct penta_orbits_periodic_v1 {
    uint32_t struct_size,reserved;uint64_t dimension,max_iterations,history_capacity;
    const double *initial,*phase_reference,*phase_direction,*state_scale;
    double period,min_period,max_period,tolerance,max_seconds,period_scale;
    void* context;penta_orbits_flow_v1 flow;
    double *state,*monodromy,*history; /* history rows:iteration,T,closure,phase,step,alpha */
    uint64_t iterations,evaluations,history_rows;double final_period,closure_residual,scaled_closure_residual,phase_residual;
    int converged;char status[96],message[512];
} penta_orbits_periodic_v1;
/* F(x,mu), Jacobian [dF/dx,dF/dmu], output-major. */
typedef int (*penta_orbits_residual_v1)(void*,const double*,double,double*,double*,char*,uint64_t);
typedef struct penta_orbits_continue_v1 {
    uint32_t struct_size,reserved;uint64_t dimension,points,max_iterations;
    const double* initial;double parameter,step,min_step,max_step,min_parameter,max_parameter,tolerance,max_seconds;
    void* context;penta_orbits_residual_v1 residual;
    double *states,*parameters,*tangents,*residuals,*step_sizes;
    uint64_t row_capacity,rows,evaluations,iterations;
    int completed;char status[96],message[512];
} penta_orbits_continue_v1;
typedef struct penta_orbits_spectrum_v1 {
    uint32_t struct_size,reserved;uint64_t dimension;
    const double* matrix;double *real_parts,*imaginary_parts;
} penta_orbits_spectrum_v1;
typedef struct penta_orbits_api_v1 {
    uint32_t struct_size,abi_version;uint64_t capabilities;
    int (*periodic)(const penta_orbits_host_v1*,penta_orbits_periodic_v1*,char*,uint64_t);
    int (*continuation)(const penta_orbits_host_v1*,penta_orbits_continue_v1*,char*,uint64_t);
    int (*spectrum)(penta_orbits_spectrum_v1*,char*,uint64_t);
} penta_orbits_api_v1;
typedef const penta_orbits_api_v1* (*penta_orbits_query_function_v1)(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
