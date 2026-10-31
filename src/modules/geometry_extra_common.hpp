#pragma once
#include "geometry_extra_api.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <functional>
#include <stdexcept>
namespace penta::geometry_extra {
struct Failure:std::runtime_error{int status;Failure(int code,const char* text):std::runtime_error(text),status(code){}};
inline void message(char* out,uint64_t capacity,const char* text){if(out&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
inline double finite(long double value){if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<double>::max())throw Failure(PENTA_NUMERIC_DOMAIN,"Geometric result is outside finite binary64 range.");const auto result=static_cast<double>(value);if(value!=0&&result==0)throw Failure(PENTA_NUMERIC_DOMAIN,"A nonzero geometric result underflows binary64.");return result;}
struct Work {
 const penta_geometry_extra_host_v1& host;uint64_t limit;std::atomic<uint64_t> used{0};
 void check()const{if(host.cancelled&&host.cancelled(host.context))throw Failure(PENTA_NUMERIC_CANCELLED,"Geometric calculation cancelled.");}
 void spend(uint64_t amount=1){auto old=used.load(std::memory_order_relaxed);do{if(old>limit||amount>limit-old)throw Failure(7,"Geometric native work budget exceeded.");}while(!used.compare_exchange_weak(old,old+amount,std::memory_order_relaxed));check();}
 void commit(){check();if(host.charge){char error[1024]{};if(host.charge(host.context,used.load(),error,sizeof(error)))throw Failure(7,error[0]?error:"Geometric host budget exceeded.");}}
};
inline void validateHost(const penta_geometry_extra_host_v1* host){if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_GEOMETRY_EXTRA_ABI_V1)throw Failure(PENTA_NUMERIC_VERSION,"Incompatible extra geometry host ABI.");}
inline void parallelFor(const penta_geometry_extra_host_v1& host,uint64_t count,uint64_t cost,const std::function<void(uint64_t,uint64_t)>& action){
 struct Task{const std::function<void(uint64_t,uint64_t)>& action;static int run(void* raw,uint64_t first,uint64_t last,char* error,uint64_t capacity)noexcept{try{static_cast<Task*>(raw)->action(first,last);return 0;}catch(const Failure& e){message(error,capacity,e.what());return e.status;}catch(const std::exception& e){message(error,capacity,e.what());return PENTA_NUMERIC_DOMAIN;}catch(...){message(error,capacity,"Unknown geometric worker failure.");return PENTA_NUMERIC_INTERNAL;}}};
 if(!count)return;
 Task task{action};char error[1024]{};const int status=host.parallel_for?host.parallel_for(host.context,count,cost,Task::run,&task,error,sizeof(error)):Task::run(&task,0,count,error,sizeof(error));if(status)throw Failure(status,error[0]?error:"Geometric worker failed.");
}
}
