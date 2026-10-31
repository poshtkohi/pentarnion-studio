#pragma once
#include <cstddef>
#include <list>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace penta::native_functions::detail {
// Loaded modules are retained for reuse, not for the lifetime of the process.
// All access to the resident owner/LRU list is synchronized. An entry is an
// eviction candidate only when this cache is its sole strong owner; an active
// callback/evaluation therefore pins its module. Retired objects are destroyed
// after unlocking, so unloading a shared library never runs under this mutex.
template<class Model>class ResidentCache {
    struct Entry {std::string key;std::shared_ptr<Model> model;};
    using List=std::list<Entry>;
    const std::size_t capacity_;
    std::mutex mutex_;
    List entries_;
    std::map<std::string,typename List::iterator> index_;
    bool room(std::shared_ptr<Model>& retired){
        if(entries_.size()<capacity_)return true;
        for(auto at=entries_.rbegin();at!=entries_.rend();++at)if(at->model.use_count()==1){
            auto victim=std::prev(at.base());retired=std::move(victim->model);
            index_.erase(victim->key);entries_.erase(victim);return true;
        }
        return false;
    }
public:
    struct Lookup {std::shared_ptr<Model> model;bool available=false;};
    explicit ResidentCache(std::size_t capacity=128):capacity_(capacity){}
    // Preflight avoids compiling a new model when every resident is genuinely
    // live. Concurrent misses can race to publish; publish checks again.
    [[nodiscard]] Lookup lookupOrMakeRoom(const std::string& key){
        std::shared_ptr<Model> retired;std::lock_guard lock(mutex_);
        if(const auto found=index_.find(key);found!=index_.end()){
            entries_.splice(entries_.begin(),entries_,found->second);
            return {found->second->model,true};
        }
        return {{},capacity_&&room(retired)};
    }
    // A duplicate publication returns the existing live model. A null result
    // means all slots became pinned while compilation was in progress.
    [[nodiscard]] std::pair<std::shared_ptr<Model>,bool> publish(const std::string& key,const std::shared_ptr<Model>& model){
        std::shared_ptr<Model> retired;std::lock_guard lock(mutex_);
        if(const auto found=index_.find(key);found!=index_.end()){
            entries_.splice(entries_.begin(),entries_,found->second);
            return {found->second->model,false};
        }
        if(!model||!capacity_||!room(retired))return {{},false};
        entries_.push_front({key,model});
        try{index_.emplace(key,entries_.begin());}catch(...){entries_.pop_front();throw;}
        return {model,true};
    }
    [[nodiscard]] std::size_t size(){std::lock_guard lock(mutex_);return entries_.size();}
};
}
