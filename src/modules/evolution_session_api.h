#pragma once
#include "evolution_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_EVOLUTION_ABI_V2 0x00020000u
typedef int(*penta_evolution_checkpoint_v2)(void*,const char*,uint64_t,char*,uint64_t);
typedef struct penta_evolution_session_v2 {
    uint32_t struct_size,abi_version,islands,migration_interval,migrants;
    uint32_t equality_count,inequality_count;
    const uint8_t* integer_coordinates;
    double constraint_tolerance;
    void* equality_context;penta_evolution_evaluate_v1 equality;
    void* inequality_context;penta_evolution_evaluate_v1 inequality;
    const char* restored_state;uint64_t restored_bytes,maximum_state_bytes;
    void* checkpoint_context;penta_evolution_checkpoint_v2 checkpoint;
}penta_evolution_session_v2;
typedef struct penta_evolution_result_v2 {
    uint32_t struct_size,feasible;
    penta_evolution_result_v1 base;
    uint64_t migrations,restored_generation,islands;
    double constraint_violation;
    double* population_constraints;uint64_t constraint_capacity;
}penta_evolution_result_v2;
typedef struct penta_evolution_api_v2 {
    uint32_t struct_size,abi_version;uint64_t capabilities;
    int(*solve)(const penta_evolution_host_v1*,const penta_evolution_request_v1*,const penta_evolution_session_v2*,penta_evolution_result_v2*,char*,uint64_t);
}penta_evolution_api_v2;
PENTA_EVOLUTION_EXPORT const penta_evolution_api_v2*penta_evolution_query_v2(uint32_t);
#ifdef __cplusplus
}
#endif
