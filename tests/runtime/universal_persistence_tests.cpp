#include "runtime/interpreter.hpp"
#include "algebra/universal.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace penta;
using Json=nlohmann::json;
int checks=0,failures=0;
void expect(bool okay,const char* text){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
void errors(const runtime::ExecutionResult& result){if(result.ok())return;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
auto values(const runtime::ExecutionResult& result){std::map<std::string,std::pair<std::string,std::string>> out;for(const auto& item:result.variables)out[item.name]={item.type,item.value};return out;}
void write(const std::filesystem::path& path,const Json& data){std::ofstream stream(path,std::ios::binary);stream<<data.dump();if(!stream)throw std::runtime_error("Could not write persistence fixture.");}
Json read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);Json data;stream>>data;return data;}
Json snapshot(const algebra::universal::DefinitionPtr& definition){return {{"hash",definition->hash()},{"json",definition->json()},{"builtin",definition->builtinKind()},{"source_json",definition->sourceJson()},{"source_hash",definition->sourceHash()}};}
Json workspace(Json variables){return {{"format","PentarnionWorkspace"},{"version",1},{"algebra","A_gamma_v1"},{"gamma",1},{"variables",std::move(variables)}};}
auto componentwiseFive(){Json table={{"format","StudioAlgebraDefinition"},{"version",1},{"name","Five independent real coordinates"},{"dimension",5},{"basis",{"a","b","c","d","e"}},{"coefficient_domain","rational"},{"unit",{"1","1","1","1","1"}},{"omitted_products","zero"},{"products",Json::array()}};for(unsigned n=0;n<5;++n)table["products"].push_back({{"left",n},{"right",n},{"terms",Json::array({Json::array({n,"1"})})}});return algebra::universal::fromJson(table.dump());}
}
int main(){
    const auto directory=std::filesystem::temp_directory_path()/("penta-universal-persistence-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        std::filesystem::create_directories(directory);runtime::ExecutionOptions options;options.dataDirectory=directory;options.odeCacheDirectory=directory/"cache";
        runtime::InterpreterSession session;
        auto saved=session.runFile("algebra H=builtin_algebra(\"quaternion\");algebra Cl=builtin_algebra(\"clifford\",options(p=2,q=1));algebra M=builtin_algebra(\"matrix2\");element<H> p=1+2*H.i-3*H.j+4*H.k;element<M> identity=1;record nested=options(item=p,definition=H);save(\"algebras.pws\");",{},options);errors(saved);expect(saved.ok(),"Save builtins, arbitrary elements, composite units and nested frozen definitions.");
        const auto before=values(saved);expect(session.submit("clear;load(\"algebras.pws\");",{},options).ok(),"Restore all frozen algebra data atomically.");
        auto restored=session.submit("assert(p==element(H,[1,2,-3,4]));assert(identity==M.E11+M.E22);assert(Cl.e3*Cl.e3==-Cl.one);assert(nested.item==p);assert(algebra_info(H).builtin==\"quaternion\");",{},options);errors(restored);expect(restored.ok(),"Restored descriptors retain trusted provenance, coefficients and qualified types.");expect(values(restored)==before,"Algebra data snapshots are lossless across workspace restoration.");
        auto corrupt=read(directory/"algebras.pws");auto& frozen=corrupt["variables"]["p"]["definition"];auto table=Json::parse(frozen["json"].get<std::string>());table["name"]="Changed frozen definition";frozen["json"]=table.dump();write(directory/"bad-hash.pws",corrupt);
        const auto rejected=session.submit("load(\"bad-hash.pws\");",{},options);expect(!rejected.ok(),"Reject a changed frozen algebra table despite an unchanged saved hash.");expect(values(rejected)==before,"Corrupt algebra snapshots cannot partially replace workspace bindings.");
        const auto different=runtime::execute("algebra O=builtin_algebra(\"octonion\");element p=O.e1;load(\"algebras.pws\",\"p\");",{},options);expect(!different.ok(),"An existing element rejects a restored value from a different algebra.");
        const auto five=componentwiseFive();const auto def=snapshot(five);Json solution={{"type","solution"},{"state_names",{"u","z"}},{"state_widths",{5,2}},{"state_metadata",Json::array({Json{{"type","element"},{"definition",def}},Json{{"type","complex"}}})},{"times",{0.,1.}},{"value",{1.,2.,3.,4.,5.,6.,7.,2.,4.,6.,8.,10.,8.,9.}}};write(directory/"generic-solution.pws",workspace({{"s",solution}}));
        runtime::InterpreterSession generic;auto loaded=generic.submit("load(\"generic-solution.pws\");",{},options);errors(loaded);expect(loaded.ok(),"Restore explicit five-coordinate user algebra and complex solution states.");
        auto used=generic.submit("element first=s.u(1);assert(algebra_info(first).name==\"Five independent real coordinates\");assert(coeff(first,4)==5);assert(coeff(s.u,4)(end)==10);assert(s.z(1)==complex(6,7));record selected=options(series=s.u);save(\"generic-series.pws\",\"selected\");",{},options);errors(used);expect(used.ok(),"Five real coordinates are preserved as their declared algebra, never inferred as Pentarnions.");
        expect(generic.submit("clear;load(\"generic-series.pws\");",{},options).ok(),"Restore a standalone generic series nested in a data record.");auto series=generic.submit("element sample=selected.series(2);assert(coeff(sample,4)==10);assert(algebra_info(sample).dimension==5);",{},options);errors(series);expect(series.ok(),"Standalone generic series retains sample indexing and frozen mathematical type.");
        auto spoof=workspace({{"D",{{"type","algebra"},{"value",def}}}});spoof["variables"]["D"]["value"]["builtin"]="quaternion";write(directory/"spoof.pws",spoof);expect(!generic.submit("load(\"spoof.pws\");",{},options).ok(),"Saved builtin hints cannot grant native privileges to a custom table.");
        auto badWidth=workspace({{"s",solution}});badWidth["variables"]["s"]["state_widths"][0]=4;write(directory/"wrong-width.pws",badWidth);expect(!generic.submit("load(\"wrong-width.pws\");",{},options).ok(),"Saved generic state widths must match the frozen descriptor exactly.");
        auto limited=options;limited.maxMatrixCoordinates=3;expect(!runtime::execute("algebra H=builtin_algebra(\"quaternion\");element p=H.i;",{},limited).ok(),"Standalone elements count every retained real coordinate against the workspace budget.");
        write(directory/"generic-record.pws",workspace({{"data",{{"type","options"},{"value",{{"x",{{"type","element"},{"definition",def},{"value",{1.,2.,3.,4.,5.}}}}}}}}}));
        auto mismatch=runtime::execute("algebra H=builtin_algebra(\"quaternion\");record data=options(x=H.i);load(\"generic-record.pws\");",{},options);expect(!mismatch.ok(),"Nested data schemas compare algebra hashes rather than only variant types.");
    }catch(const std::exception& error){++failures;std::cerr<<"Unexpected exception: "<<error.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);std::cout<<checks<<" universal algebra persistence checks, "<<failures<<" failures\n";return failures?1:0;
}
