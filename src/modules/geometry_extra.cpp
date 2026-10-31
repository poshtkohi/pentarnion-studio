#ifndef PENTA_NUMERIC_MODULE_BUILD
#define PENTA_NUMERIC_MODULE_BUILD
#endif
#include "geometry_extra_api.h"
extern "C" {
int penta_extra_mesh(const penta_geometry_extra_host_v1*,const penta_mesh_request_v1*,penta_mesh_result_v1*,char*,uint64_t);
int penta_extra_forms(const penta_geometry_extra_host_v1*,const penta_form_request_v1*,penta_form_result_v1*,char*,uint64_t);
int penta_extra_laplace(const penta_chart_point_v1*,const double*,const double*,double*,char*,uint64_t);
int penta_extra_heat(const penta_geometry_extra_host_v1*,const penta_surface_heat_request_v1*,penta_surface_heat_result_v1*,char*,uint64_t);
}
namespace {const penta_geometry_extra_api_v1 api{sizeof(penta_geometry_extra_api_v1),PENTA_GEOMETRY_EXTRA_ABI_V1,penta_extra_mesh,penta_extra_forms,penta_extra_laplace,penta_extra_heat};}
extern "C" PENTA_NUMERIC_EXPORT const penta_geometry_extra_api_v1* penta_geometry_extra_query_v1(uint32_t version){return version==PENTA_GEOMETRY_EXTRA_ABI_V1?&api:nullptr;}
