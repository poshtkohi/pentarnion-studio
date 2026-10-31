#pragma once
#include "experiment_bundle.hpp"
#include <limits>
namespace penta::research::archive {
using Json=nlohmann::json;
struct Extension {
    Json payload,settings,workspace,history=Json::array(),calls=Json::array(),plots=Json::array();
    bool historyComplete=true,callsComplete=true;
    struct Embedded {std::string label,path,bytes,platform="data";};
    std::vector<Embedded> artifacts;
};
struct Inspection {bundle::Inspection base;std::size_t history=0,calls=0,embedded=0;bool historyComplete=false,callsComplete=false,gammaBound=false;std::string scientificHash,integrityHash;};
Json build(const bundle::Capture&,const Extension&,const bundle::Limits& = {});
Json migrate(const Json& version1,const bundle::Context&,const bundle::Limits& = {});
Inspection inspect(const Json&,const bundle::Context&,const bundle::Limits& = {});
Json payload(const Json&,const bundle::Context&,bool allowRealContext=false,const bundle::Limits& = {});
std::string artifact(const Json&,const std::string& label,const bundle::Context&,const bundle::Limits& = {});
Json read(const std::filesystem::path&,const bundle::Limits& = {});
struct CatalogueOptions {std::size_t maximumFiles=256;bool recursive=false;std::string nameContains,family,status,scalarPath;double minimum=-std::numeric_limits<double>::infinity(),maximum=std::numeric_limits<double>::infinity();};
Json catalogue(const std::filesystem::path&,const CatalogueOptions&,const bundle::Context&,const bundle::Limits& = {});
}
