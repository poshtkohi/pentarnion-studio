#include "universal_import_data.hpp"
#include "runtime/data_io.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace penta::language {
namespace {
using Json=nlohmann::json;
void require(bool value,const std::string& message){if(!value)throw algebra::universal::Error("A4020",message);}
void cancelled(const algebra::universal::ImportOptions& options){if(options.cancellation&&options.cancellation->load(std::memory_order_relaxed))throw algebra::universal::Error("A4026","Algebra table import was cancelled.");}
}
algebra::universal::DefinitionPtr algebraDefinitionFromTable(std::string_view metadata,std::string_view tableBytes,
    const std::filesystem::path& tableFormat,const std::string& sheet,const algebra::universal::ImportOptions& options){
    cancelled(options);
    require(options.maxBytes>=1&&options.maxBytes<=64*1024*1024&&options.maxDimension>=1&&options.maxDimension<=64&&options.maxTerms>=1&&options.maxTerms<=262144,"Invalid algebra table-import bounds.");
    require(metadata.size()<=options.maxBytes&&tableBytes.size()<=options.maxBytes-metadata.size(),"Algebra metadata/table snapshots exceed the byte allowance.");
    try{
        std::vector<std::set<std::string>> keys;std::size_t nodes=0;
        auto definition=Json::parse(metadata,[&](int depth,Json::parse_event_t event,Json& value){
            cancelled(options);require(depth<=64&&++nodes<=2000000,"Algebra metadata JSON exceeds its nesting/node allowance.");
            if(event==Json::parse_event_t::object_start)keys.emplace_back();
            else if(event==Json::parse_event_t::object_end){require(!keys.empty(),"Invalid algebra metadata object nesting.");keys.pop_back();}
            else if(event==Json::parse_event_t::key){require(!keys.empty(),"Invalid algebra metadata key nesting.");const auto key=value.get<std::string>();require(keys.back().insert(key).second,"Duplicate algebra metadata field: "+key+".");}
            return true;
        });
        require(definition.is_object(),"Algebra metadata must be a JSON object.");
        require(definition.contains("products")&&definition["products"].is_array()&&definition["products"].empty()&&!definition.contains("tensor"),"CSV/XLSX metadata requires exactly an empty products array and no tensor; existing products cannot be overridden.");
        require(definition.contains("coefficient_domain")&&definition["coefficient_domain"]=="floating","CSV/XLSX numeric product rows require coefficient_domain=floating; use JSON rational strings for exact tables.");
        require(definition.contains("dimension")&&definition["dimension"].is_number_integer(),"Algebra metadata requires an integer dimension.");
        const auto dimension=definition["dimension"].get<std::int64_t>();require(dimension>=1&&static_cast<std::uint64_t>(dimension)<=options.maxDimension,"Algebra metadata dimension exceeds the configured allowance.");
        runtime::dataio::Options dataOptions;dataOptions.maxElements=options.maxTerms*4;dataOptions.maxFileBytes=options.maxBytes;
        dataOptions.maxUncompressedBytes=options.maxBytes;dataOptions.cancellation=options.cancellation;
        const auto table=runtime::dataio::readMatrixBytes(tableBytes,tableFormat,sheet,dataOptions);
        require(table.values.empty()||table.columns==4,"Algebra product data must have four columns [left1,right1,output1,coefficient].");
        require(table.rows<=options.maxTerms,"Algebra product data exceeds the term allowance.");
        std::map<std::pair<std::size_t,std::size_t>,Json> products;std::set<std::tuple<std::size_t,std::size_t,std::size_t>> terms;
        for(std::size_t row=0;row<table.rows;++row){
            if((row&255u)==0)cancelled(options);
            std::size_t indices[3]{};
            for(std::size_t column=0;column<3;++column){const auto value=table.values[row*4+column];require(std::isfinite(value)&&value>=1&&value<=static_cast<double>(dimension)&&value==std::trunc(value),"Algebra table basis indices must be integral one-based values within the declared dimension.");indices[column]=static_cast<std::size_t>(value)-1;}
            const auto coefficient=table.values[row*4+3];require(std::isfinite(coefficient),"Algebra table coefficients must be finite.");
            require(terms.emplace(indices[0],indices[1],indices[2]).second,"A CSV/XLSX multiplication coefficient occurs more than once.");
            const auto key=std::pair(indices[0],indices[1]);auto [entry,inserted]=products.try_emplace(key,Json::array());(void)inserted;
            entry->second.push_back(Json::array({indices[2],coefficient}));
        }
        definition["products"]=Json::array();for(auto& [indices,entries]:products)definition["products"].push_back({{"left",indices.first},{"right",indices.second},{"terms",std::move(entries)}});
        const auto merged=definition.dump();require(merged.size()<=options.maxBytes,"Constructed algebra definition exceeds the validated JSON byte allowance.");
        return algebra::universal::fromJson(merged,options);
    }catch(const runtime::dataio::Error& error){cancelled(options);throw algebra::universal::Error("A4020",std::string("Invalid algebra CSV/XLSX data: ")+error.what());}
    catch(const Json::exception& error){throw algebra::universal::Error("A4020",std::string("Invalid algebra metadata JSON: ")+error.what());}
}
}
