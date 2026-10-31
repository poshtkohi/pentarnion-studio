#ifndef PENTA_GRAPH_API_H
#define PENTA_GRAPH_API_H
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif

#define PENTA_GRAPH_ABI_V1 0x00010000u
enum penta_graph_operation_v1 {
    PENTA_GRAPH_INFO=1, PENTA_GRAPH_COMPONENTS, PENTA_GRAPH_SHORTEST_PATH,
    PENTA_GRAPH_SPANNING_TREE, PENTA_GRAPH_TOPOSORT, PENTA_GRAPH_CENTRALITY,
    PENTA_GRAPH_LAPLACIAN, PENTA_GRAPH_ADJACENCY, PENTA_GRAPH_LAYOUT,
    PENTA_GRAPH_CYCLE
};
enum penta_graph_outcome_v1 {
    PENTA_GRAPH_COMPLETE=0, PENTA_GRAPH_UNREACHABLE=1,
    PENTA_GRAPH_NEGATIVE_CYCLE=2, PENTA_GRAPH_CYCLIC=3, PENTA_GRAPH_FOREST=4
};
enum penta_graph_error_v1 { PENTA_GRAPH_WORK_LIMIT=7 };
typedef struct penta_graph_host_v1 {
    uint32_t struct_size,abi_version;
    void* context;
    int (*cancelled)(void*);
    int (*charge)(void*,uint64_t,char*,uint64_t);
} penta_graph_host_v1;
typedef struct penta_graph_request_v1 {
    uint32_t struct_size,operation,directed,multigraph;
    uint64_t nodes,edges;
    const uint64_t* endpoints; /* Row-major edge pairs; IDs are one-based. */
    const double* weights;    /* Null means unit weights. */
    uint64_t source,target,max_work;
    uint32_t kind,normalized,dimensions,reserved;
    double radius;
} penta_graph_request_v1;
typedef struct penta_graph_result_v1 {
    uint32_t struct_size,outcome;
    double* output;
    uint64_t output_capacity,output_count,rows,columns,work_units;
    uint64_t extra[4];
    double scalar;
    char status[64],method[64];
} penta_graph_result_v1;
typedef struct penta_graph_api_v1 {
    uint32_t struct_size,abi_version;
    const char *module_name,*build_identity;
    int (*execute)(const penta_graph_host_v1*,const penta_graph_request_v1*,
                   penta_graph_result_v1*,char*,uint64_t);
} penta_graph_api_v1;
typedef const penta_graph_api_v1* (*penta_graph_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_graph_api_v1* penta_graph_query_v1(uint32_t);

#ifdef __cplusplus
}
#endif
#endif
