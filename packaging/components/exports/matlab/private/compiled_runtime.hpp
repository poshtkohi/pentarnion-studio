#pragma once
#include "mx_value.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pm {
struct Slot { V value; };
struct Env {
    std::map<std::string,std::shared_ptr<Slot>> variables;
    V get(const std::string& name) const;
    void put(const std::string& name,const V& value);
};
using EnvPtr=std::shared_ptr<Env>;
using CompiledFunction=Outs(*)(EnvPtr,const Args&,int);
struct FunctionDef {
    int id;
    CompiledFunction function;
    std::vector<std::string> parameters,locals,captures,persistent;
};
extern const FunctionDef definitions[];
extern const size_t definitionCount;
V first(const Outs& out);
V output(const Outs& out,size_t index);
Args join(std::initializer_list<Args> pieces);
Outs apply(const V& value,const Args& args,int requested=1);
Args columns(const V& value);
bool matches(const V& key,const V& pattern);
Outs results(EnvPtr env,const std::vector<std::string>& names);
Outs run(int id,EnvPtr parent,const Args& arguments,int requested=1);
V handle(int id,EnvPtr parent,bool snapshot=false);
Outs callback(const V& context,const Args& arguments,int requested);
void release(const V& token);
void cleanup();
}
