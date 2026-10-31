#ifndef PENTA_ITERATION_API_H
#define PENTA_ITERATION_API_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_ITERATION_ABI_V1 0x00010000u
#define PENTA_ITERATION_CAPABILITY_V1 UINT64_C(0x100000000)
enum penta_iteration_phase_v1 {PENTA_ITERATION_INIT=0,PENTA_ITERATION_ITER=1,PENTA_ITERATION_DONE=2};
enum penta_iteration_event_v1 {PENTA_ITERATION_EVENT_ITERATION=1,PENTA_ITERATION_EVENT_EVALUATION=2,PENTA_ITERATION_EVENT_GENERATION=3};
enum penta_iteration_flags_v1 {PENTA_ITERATION_HAS_OBJECTIVE=1,PENTA_ITERATION_HAS_RESIDUAL_NORM=2,PENTA_ITERATION_HAS_STEP_NORM=4,PENTA_ITERATION_ACCEPTED=8};
/* Borrowed immutable driver-thread snapshot. x is valid only during this call.
 * No implicit objective/model evaluation is performed for an observer.
 * event labels distinguish accepted-iteration/generation information from an
 * evaluation stream (NLopt/KINSOL/TOMS748). Flags identify available scalars.
 * Return0 continue,1 clean user stop,negative error with bounded error text.
 * A return1 from DONE is ignored. No exceptions may cross this C ABI. */
typedef struct penta_iteration_snapshot_v1 {
    uint32_t struct_size,abi_version,phase,event,flags,reserved;
    uint64_t iteration,evaluations,start_index,dimension;
    const double* x;
    double objective,residual_norm,step_norm,elapsed_seconds;
} penta_iteration_snapshot_v1;
typedef int (*penta_iteration_observer_v1)(void*,const penta_iteration_snapshot_v1*,char*,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
