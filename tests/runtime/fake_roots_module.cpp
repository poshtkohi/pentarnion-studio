#define PENTA_ROOTS_MODULE_BUILD
#include "modules/roots_api.h"
namespace {const penta_roots_api_v1 api{sizeof(penta_roots_api_v1),PENTA_ROOTS_ABI_V1+1,"incompatible roots fixture","test only",0,nullptr};}
extern "C" PENTA_ROOTS_EXPORT const penta_roots_api_v1* penta_roots_query_v1(uint32_t){return &api;}
