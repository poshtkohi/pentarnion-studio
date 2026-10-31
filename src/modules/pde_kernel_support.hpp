#pragma once
#include "pde_api.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace penta::pde::kernel {
inline void message(char* output,uint64_t capacity,const char* text){
    if(output&&capacity){const auto count=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(output,text,count);output[count]=0;}
}
template<class Function>
void range(const penta_pde_host_v1* host,uint64_t count,uint64_t cost,Function&& function,uint64_t minimum=64){
    struct Task{
        Function& function;
        static int evaluate(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity)noexcept{
            try{static_cast<Task*>(raw)->function(begin,end);return PENTA_NUMERIC_OK;}
            catch(const std::exception& failure){message(error,capacity,failure.what());return PENTA_NUMERIC_INTERNAL;}
            catch(...){message(error,capacity,"Unknown PDE range failure.");return PENTA_NUMERIC_INTERNAL;}
        }
    } task{function};
    const auto* parallel=host->struct_size>=sizeof(penta_pde_parallel_host_v1)?reinterpret_cast<const penta_pde_parallel_host_v1*>(host):nullptr;
    if(parallel&&parallel->parallel_for&&parallel->threads_granted>1&&count>=minimum){
        char detail[2048]{};
        if(parallel->parallel_for(host->context,count,cost,Task::evaluate,&task,detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"PDE parallel range failed.");
    }else function(0,count);
}
}
