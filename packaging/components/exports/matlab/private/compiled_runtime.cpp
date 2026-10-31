#include "compiled_runtime.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace pm {
V Env::get(const std::string& name) const {
    auto i=variables.find(name);
    if(i==variables.end()) throw std::runtime_error("Unbound compiled adapter variable: "+name);
    return i->second->value;
}
void Env::put(const std::string& name,const V& value) {
    auto& slot=variables[name];if(!slot)slot=std::make_shared<Slot>();slot->value=value;
}
V first(const Outs& out) { return out.empty()?V():out.front(); }
V output(const Outs& out,size_t index) { return index<out.size()?out[index]:V(); }
Args join(std::initializer_list<Args> pieces) { Args out;for(const auto& part:pieces)out.insert(out.end(),part.begin(),part.end());return out; }
Outs apply(const V& value,const Args& args,int requested) {
    if(truth(one("isa",{value,V("function_handle")}))) {
        Args full{value};full.insert(full.end(),args.begin(),args.end());return call("feval",full,requested);
    }
    return indexMany(value,args,false,requested);
}
Args columns(const V& value) {
    Args out;
    const size_t count=static_cast<size_t>(one("size",{value,V(2.0)}).scalar());
    for(size_t k=0;k<count;++k)out.push_back(index(value,{V(":"),V(static_cast<double>(k+1))}));
    return out;
}
bool matches(const V& key,const V& pattern) {
    if(mxIsCell(pattern.raw())) {
        for(size_t k=0;k<mxGetNumberOfElements(pattern.raw());++k)
            if(truth(one("isequal",{key,V(mxGetCell(pattern.raw(),k))})))return true;
        return false;
    }
    return truth(one("isequal",{key,pattern}));
}
Outs results(EnvPtr env,const std::vector<std::string>& names) {
    Outs out;for(const auto& name:names)out.push_back(env->get(name));return out;
}
static std::map<std::pair<int,std::string>,std::shared_ptr<Slot>> persistentSlots;
Outs run(int id,EnvPtr parent,const Args& arguments,int requested) {
    if(id<0||static_cast<size_t>(id)>=definitionCount)throw std::runtime_error("Unknown compiled adapter function");
    const auto& definition=definitions[id];auto env=std::make_shared<Env>();
    if(parent)for(const auto& name:definition.captures){auto i=parent->variables.find(name);if(i!=parent->variables.end())env->variables[name]=i->second;}
    for(const auto& name:definition.locals)if(!env->variables.count(name))env->variables[name]=std::make_shared<Slot>();
    for(const auto& name:definition.persistent){auto& slot=persistentSlots[{id,name}];if(!slot)slot=std::make_shared<Slot>();env->variables[name]=slot;}
    for(size_t k=0;k<definition.parameters.size();++k) {
        const auto& name=definition.parameters[k];
        if(name=="varargin") {
            Args tail;for(size_t n=k;n<arguments.size();++n)tail.push_back(arguments[n]);
            env->put(name,matrix({tail},true));
        } else env->put(name,k<arguments.size()?arguments[k]:V());
    }
    env->put("nargin",V(static_cast<double>(arguments.size())));
    env->put("nargout",V(static_cast<double>(requested)));
    return definition.function(env,arguments,requested);
}
struct Closure { int id; EnvPtr environment; };
static std::map<uint64_t,Closure> closures;
static uint64_t nextToken=1;
static bool shuttingDown=false;
V handle(int id,EnvPtr parent,bool snapshot) {
    auto env=std::make_shared<Env>();
    for(const auto& name:definitions[id].captures) {
        auto i=parent->variables.find(name);if(i==parent->variables.end())continue;
        if(snapshot){auto slot=std::make_shared<Slot>();slot->value=i->second->value;env->variables[name]=slot;}
        else env->variables[name]=i->second;
    }
    const uint64_t token=nextToken++;closures.emplace(token,Closure{id,env});mexLock();
    try {
        V context=one("penta_runtime_context",{V(static_cast<double>(token))});
        return one("penta_runtime_bind",{context});
    } catch(...) {closures.erase(token);mexUnlock();throw;}
}
Outs callback(const V& context,const Args& arguments,int requested) {
    const uint64_t token=static_cast<uint64_t>(member(context,"token").scalar());
    auto i=closures.find(token);if(i==closures.end())throw std::runtime_error("Expired compiled adapter closure");
    const Closure closure=i->second;return run(closure.id,closure.environment,arguments,requested);
}
void release(const V& tokenValue) {
    if(shuttingDown)return;
    const uint64_t token=static_cast<uint64_t>(tokenValue.scalar());
    auto i=closures.find(token);if(i==closures.end())return;
    Closure saved=std::move(i->second);closures.erase(i);mexUnlock();
}
void cleanup(){shuttingDown=true;closures.clear();persistentSlots.clear();}
}
