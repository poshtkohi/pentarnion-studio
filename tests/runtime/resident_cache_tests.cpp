#include "native_functions/resident_cache.hpp"
#include <atomic>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
int checks=0;void require(bool yes,const char* text){++checks;if(!yes)throw std::runtime_error(text);}
struct Model {int id;std::function<void()> destroyed;explicit Model(int value,std::function<void()> action={}):id(value),destroyed(std::move(action)){}~Model(){if(destroyed)destroyed();}};
using Cache=penta::native_functions::detail::ResidentCache<Model>;
}
int main(){try{
    Cache cache(3);std::atomic_int retired=0;
    require(bool(cache.publish("A",std::make_shared<Model>(1)).first),"first admission");
    require(bool(cache.publish("B",std::make_shared<Model>(2,[&]{require(cache.size()==2,"retired model destructor runs after unlock");++retired;})).first),"second admission");
    require(bool(cache.publish("C",std::make_shared<Model>(3)).first),"third admission");
    auto a=cache.lookupOrMakeRoom("A").model;
    require(cache.lookupOrMakeRoom("D").available&&retired==1,"least-recent unreferenced model retired");
    auto d=cache.publish("D",std::make_shared<Model>(4)).first;
    auto c=cache.lookupOrMakeRoom("C").model;
    require(a&&c&&d&&a->id==1&&c->id==3&&d->id==4,"all externally held models stay alive");
    require(!cache.lookupOrMakeRoom("E").available,"capacity remains bounded for genuinely live models");
    require(!cache.publish("E",std::make_shared<Model>(5)).first,"racing publication cannot bypass a full live cache");
    require(cache.publish("A",std::make_shared<Model>(99)).first==a,"duplicate key reuses the existing pinned model");
    c.reset();require(cache.lookupOrMakeRoom("E").available,"released model permits bounded admission");
    require(bool(cache.publish("E",std::make_shared<Model>(5)).first),"replacement admitted after release");
    require(cache.lookupOrMakeRoom("A").model==a&&cache.lookupOrMakeRoom("D").model==d,"live model identities survive replacement");
    Cache concurrent(8);auto pinned=concurrent.publish("pinned",std::make_shared<Model>(-1)).first;
    std::atomic_size_t failures=0,acquisitions=0;std::vector<std::thread> workers;
    for(unsigned worker=0;worker<8;++worker)workers.emplace_back([&,worker]{try{for(unsigned iteration=0;iteration<250;++iteration){const auto key=std::to_string(worker)+":"+std::to_string(iteration);auto lookup=concurrent.lookupOrMakeRoom(key);if(!lookup.available){std::this_thread::yield();continue;}auto model=std::move(lookup.model);if(!model)model=concurrent.publish(key,std::make_shared<Model>(int(worker*1000+iteration))).first;if(!model)continue;++acquisitions;const auto identity=model->id;for(unsigned repeat=0;repeat<3;++repeat){std::this_thread::yield();if(model->id!=identity||concurrent.lookupOrMakeRoom("pinned").model!=pinned||concurrent.size()>8)++failures;}}}catch(...){++failures;}});
    for(auto& worker:workers)worker.join();
    require(acquisitions>0&&failures==0,"concurrent eviction preserves active handles, identities and bounded size");
    require(concurrent.lookupOrMakeRoom("pinned").model==pinned,"oldest active model remains reusable after concurrent pressure");
    std::cout<<"PASS "<<checks<<" resident native module lifetime/concurrency checks\n";
}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
