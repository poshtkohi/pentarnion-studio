#ifndef PENTA_VECTOR_PLOT_API_H
#define PENTA_VECTOR_PLOT_API_H
#include "numeric_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PENTA_VECTOR_PLOT_ABI_V1 0x00010000u
/* Screen-space CPU projection commands. No Qt or C++ objects cross this ABI. */
typedef struct penta_vector_command_v1 {
    uint32_t kind,style,font_family,latex; /* line,triangle,text,circle */
    double x[3],y[3],color[3],clip[4],width,height;
    const char* text;uint64_t text_bytes;
} penta_vector_command_v1;
typedef struct penta_vector_host_v1 {
    uint32_t struct_size,abi_version;void* context;
    int (*cancelled)(void*);
    int (*append)(void*,const char*,uint64_t);
} penta_vector_host_v1;
typedef struct penta_vector_request_v1 {
    uint32_t struct_size,format,width,height,dpi; /* format1 SVG,2 PDF */
    uint64_t commands,max_bytes;const penta_vector_command_v1* data;
    const char* metadata;uint64_t metadata_bytes;
} penta_vector_request_v1;
typedef struct penta_vector_plot_api_v1 {
    uint32_t struct_size,abi_version;const char* module_name;
    int (*render)(const penta_vector_host_v1*,const penta_vector_request_v1*,char*,uint64_t);
} penta_vector_plot_api_v1;
typedef const penta_vector_plot_api_v1* (*penta_vector_plot_query_function_v1)(uint32_t);
PENTA_NUMERIC_EXPORT const penta_vector_plot_api_v1* penta_vector_plot_query_v1(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
