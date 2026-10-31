#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_BIFURCATION_ABI_V1 0x10000u
enum penta_bifurcation_kind_v1 {PENTA_BIFURCATION_FOLD=0,PENTA_BIFURCATION_HOPF=1,PENTA_BIFURCATION_FLIP=2};
typedef struct penta_bifurcation_host_v1 {uint32_t struct_size,abi_version;void* context;int(*cancelled)(void*);int(*charge)(void*,uint64_t,char*,uint64_t);}penta_bifurcation_host_v1;
/* Jet columns: every real state coordinate, then selected real parameter.
   J is n*(n+1), H is n*(n+1)*(n+1), output-major/row-major. Null J/H
   request only values/first derivatives respectively. */
typedef int(*penta_bifurcation_jet_v1)(void*,const double*,double,double*,double*,double*,char*,uint64_t);
typedef int(*penta_bifurcation_flow_v1)(void*,const double*,double,double,double*,double*,double*,char*,uint64_t);
typedef struct penta_bifurcation_request_v1 {
    uint32_t struct_size,kind;uint64_t dimension,max_iterations,history_capacity;
    const double* initial;double parameter,auxiliary,min_parameter,max_parameter,min_auxiliary,max_auxiliary;
    double tolerance,spectral_tolerance,transversality_tolerance,fd_step,parameter_step,fd_agreement,max_seconds;
    void* context;penta_bifurcation_jet_v1 jet;penta_bifurcation_flow_v1 flow;
    double *state,*vector_real,*vector_imag,*matrix,*spectrum_real,*spectrum_imag,*history;
    uint64_t iterations,evaluations,history_rows;double final_parameter,final_auxiliary,residual,critical_real,critical_imag;
    double crossing,quadratic,step_difference,neutral_error,amplitude;
    uint32_t converged,simple,transverse,criticality_computed,nondegenerate,step_refined,other_critical_modes;
    char status[96],message[512];
}penta_bifurcation_request_v1;
typedef struct penta_bifurcation_api_v1 {uint32_t struct_size,abi_version;uint64_t capabilities;int(*refine)(const penta_bifurcation_host_v1*,penta_bifurcation_request_v1*,char*,uint64_t);}penta_bifurcation_api_v1;
typedef const penta_bifurcation_api_v1*(*penta_bifurcation_query_function_v1)(uint32_t);
#ifdef __cplusplus
}
#endif
