#ifndef PENTA_GRAPH_TOOLS_API_H
#define PENTA_GRAPH_TOOLS_API_H
#include "graph_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_GRAPH_TOOLS_ABI_V1 0x00010000u
enum penta_graph_tools_operation_v1 {PENTA_GRAPH_TOOLS_SPARSE=1,PENTA_GRAPH_TOOLS_GENERATE=2};
enum penta_graph_tools_kind_v1 {PENTA_GRAPH_SPARSE_ADJACENCY=1,PENTA_GRAPH_SPARSE_INCIDENCE=2,PENTA_GRAPH_SPARSE_LAPLACIAN=3,PENTA_GRAPH_GENERATE_PATH=10,PENTA_GRAPH_GENERATE_CYCLE=11,PENTA_GRAPH_GENERATE_GRID=12,PENTA_GRAPH_GENERATE_GNM=13};
typedef struct penta_graph_tools_request_v1 {
    uint32_t struct_size,operation,kind,directed,multigraph,weighted;
    uint64_t nodes,edges;
    const uint64_t* endpoints;
    const double* weights;
    uint64_t seed,requested_edges,grid_rows,grid_columns,max_work;
    double generated_weight;
} penta_graph_tools_request_v1;
typedef struct penta_graph_tools_result_v1 {
    uint32_t struct_size;
    uint64_t* row_indices; /* Zero-based canonical CSC. */
    uint64_t* column_pointers;
    double* values;
    uint64_t* endpoints; /* Generated one-based physical edge rows. */
    uint64_t entry_capacity,pointer_capacity,endpoint_capacity;
    uint64_t rows,columns,entries,edges,work_units;
} penta_graph_tools_result_v1;
typedef struct penta_graph_tools_api_v1 {
    uint32_t struct_size,abi_version;
    const char* module_name;
    int (*execute)(const penta_graph_host_v1*,const penta_graph_tools_request_v1*,penta_graph_tools_result_v1*,char*,uint64_t);
} penta_graph_tools_api_v1;
typedef const penta_graph_tools_api_v1* (*penta_graph_tools_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_graph_tools_api_v1* penta_graph_tools_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
