#define PENTA_NUMERIC_MODULE_BUILD
#include "modules/numeric_api.h"
extern "C" PENTA_NUMERIC_EXPORT const penta_numeric_api_v1* penta_numeric_query_v1(uint32_t){
    static const penta_numeric_api_v1 incompatible{sizeof(penta_numeric_api_v1),PENTA_NUMERIC_ABI_V1+1,"incompatible-test","test-only",0,nullptr,nullptr,nullptr};return &incompatible;
}
