#pragma once
#include "code_export.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
namespace penta::exporting {
inline bool loadCompiledRuntimeFiles(Options& options,Target target,std::string& error){
#ifdef PENTA_DISTRIBUTION_BUILD
    std::filesystem::path sdk;
#ifdef _WIN32
    if(const auto* path=_wgetenv(L"PENTA_RUNTIME_SDK"))sdk=path;
#else
    if(const auto* path=std::getenv("PENTA_RUNTIME_SDK"))sdk=path;
#endif
    if(sdk.empty()||!sdk.is_absolute()){error="The installed compiled target runtime is unavailable. Start the exporter using its installed launcher.";return false;}
    const auto folder=sdk/(target==Target::Python?"python":"matlab");
    const std::vector<std::string> names=target==Target::Python?std::vector<std::string>{"penta_export_runtime.pyd"}:std::vector<std::string>{"penta_runtime_native.mexw64","penta_runtime.m","penta_dual.m","penta_runtime_bind.m","penta_runtime_context.m"};
    std::map<std::string,std::string> resources;
    for(const auto& name:names){
        const auto path=folder/name;std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
        if(ec||!size||size>64u*1024u*1024u){error="A compiled target runtime component is missing or invalid: "+name;return false;}
        std::ifstream file(path,std::ios::binary);if(!file){error="Cannot read compiled target runtime component: "+name;return false;}
        std::string bytes{std::istreambuf_iterator<char>(file),{}};
        if(bytes.size()!=size){error="A compiled target component changed during export: "+name;return false;}
        resources.emplace(name,std::move(bytes));
    }
    options.compiledRuntimeFiles=std::move(resources);
#else
    (void)options;(void)target;(void)error;
#endif
    return true;
}
}
