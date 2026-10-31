#pragma once
#include "native_ode.hpp"
#include <chrono>
#include <string_view>

namespace penta::ode::detail {
struct Generated {std::string source;std::string normalized;std::size_t coordinates=0,parameters=0;};
[[nodiscard]] Generated generate(const language::OdeDefinition&);
[[nodiscard]] std::string sha256(std::string_view);
[[nodiscard]] std::string fileHash(const std::filesystem::path&);
void checkCancelled(const NativeOptions&);
// Internal process launcher, shared by cache construction and launch regression
// probes; not exposed by the Pentarnion language or public interpreter API.
[[nodiscard]] std::string runCompilerProcess(const std::filesystem::path& compiler,
    const std::vector<std::filesystem::path>& arguments,const std::filesystem::path& cwd,
    const NativeOptions&);
using EvaluateFunction=int(*)(double,double,const double*,const double*,double*,double*,char*,std::size_t);
struct Model {
    void* library=nullptr;
    EvaluateFunction evaluate=nullptr;
    std::size_t coordinates=0,parameters=0;
    bool cacheHit=false;std::string key;
    ~Model();
};
[[nodiscard]] std::unique_ptr<Model> compile(const Generated&,const NativeOptions&);
} // namespace penta::ode::detail
