#pragma once
#include "runtime/data_io.hpp"
#include <nlohmann/json.hpp>
#include <functional>
#include <stdexcept>
namespace penta::research::bundle {
using Json=nlohmann::json;
struct Error:std::runtime_error{int status;Error(int code,const std::string& text):std::runtime_error(text),status(code){}};
struct Context{std::string algebra="A_gamma_v1",language="0.2";double gamma=1;unsigned precision=53,integer=64;};
struct Source{std::string id,role,bytes,location;};
struct Artifact{std::string label,path,hash;uint64_t size=0;};
struct Capture{Context context;std::string name="experiment";Json payload,settings,scientificProvenance,executionProvenance;std::vector<Source> sources;std::vector<std::pair<std::string,std::string>> additionalLocations;std::vector<Artifact> artifacts;};
struct Limits{std::size_t maximumBytes=64*1024*1024,maximumSourceBytes=12*1024*1024,maximumArtifactBytes=64*1024*1024;std::shared_ptr<std::atomic_bool> cancellation;};
struct Inspection{int version=0;bool integrity=false,contextMatch=false,compatible=false,migration=false,pentarnion=false;std::string name,scientificHash,integrityHash,mainHash,reason;std::size_t sources=0,artifacts=0;Context context;};
Json build(const Capture&,const Limits& = {});
Inspection inspect(const Json&,const Context&,const Limits& = {});
Json read(const std::filesystem::path&,const Limits& = {});
void save(const std::filesystem::path&,const Json&,bool overwrite,const Limits& = {});
Json payload(const Json&,const Context&,bool allowRealContext,const Limits& = {});
std::string source(const Json&,const std::string& id,const Context&,const Limits& = {});
Artifact artifact(const std::filesystem::path& root,const std::string& label,const std::string& relative,const Limits& = {});
// Read bytes through the same verified handle/path rules as artifact().
std::string artifactBytes(const std::filesystem::path& root,const std::string& relative,const Limits& = {});
bool containsPentarnion(const Json&,const Limits& = {});
Json scientificProjection(const Json&,const Limits& = {});
}
