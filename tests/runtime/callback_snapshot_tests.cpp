#include "runtime/interpreter.hpp"
#include "runtime/data_io.hpp"
#include "ode/native_internal.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <locale>
#include <random>
#include <sstream>
#include <stdexcept>

namespace {
unsigned checks=0;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);++checks;}
void diagnostics(const penta::runtime::ExecutionResult& result){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
bool replaceSource(nlohmann::json& value){
    if(value.is_object()&&value.contains("source")&&value.contains("fingerprint")&&value.contains("sha256")){
        const std::string source="native function real square(real x){display(999);return x*x;}";
        value["source"]=source;value["sha256"]=penta::ode::detail::sha256(source);return true;
    }
    if(value.is_structured())for(auto& entry:value)if(replaceSource(entry))return true;
    return false;
}
}
int main(){try{
    const auto directory=std::filesystem::temp_directory_path()/("penta-callback-snapshots-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);
    penta::runtime::ExecutionOptions options;options.dataDirectory=directory;options.odeCacheDirectory=directory/"cache";
    penta::runtime::InterpreterSession original;
    const auto saved=original.submit("native function real square(real x){return x*x;}record stored=options(f=@square);save(\"callback.json\",\"stored\");",{},options);if(!saved.ok())diagnostics(saved);check(saved.ok(),"Pure named numerical callbacks save as checked source.");
    penta::runtime::InterpreterSession loaded;const auto loadedResult=loaded.submit("load(\"callback.json\");",{},options);if(!loadedResult.ok())diagnostics(loadedResult);check(loadedResult.ok(),"A fresh session restores callback type/source.");check(loadedResult.nativeCompilation.compilerAttempts==0,"Loading source does not compile or execute callbacks.");
    const auto invoked=loaded.submit("assert(stored.f(3.0)==9.0);",{},options);if(!invoked.ok())diagnostics(invoked);check(invoked.ok(),"An explicitly invoked restored native callback works.");
    auto data=nlohmann::json::parse(penta::runtime::dataio::readBytes(directory/"callback.json"));check(replaceSource(data),"A callback source is present in the saved envelope.");penta::runtime::dataio::writeBytesAtomic(directory/"unsafe.json",data.dump());
    penta::runtime::InterpreterSession rejected;const auto unsafe=rejected.submit("load(\"unsafe.json\");",{},options);check(!unsafe.ok()&&unsafe.output.empty(),"A recomputed hash cannot authorize an effectful numerical callback.");
    std::mt19937_64 engine(1234567);for(unsigned n=0;n<79;++n)(void)engine();std::ostringstream encoded;encoded.imbue(std::locale::classic());encoded<<engine;
    penta::runtime::InterpreterSession a,b;a.restoreRandomState(encoded.str());b.restoreRandomState(encoded.str());const auto one=a.submit("display(rand(1,7));",{},options),two=b.submit("display(rand(1,7));",{},options);check(one.ok()&&two.ok()&&one.output==two.output,"An explicitly restored numeric RNG state replays advanced unseeded draws.");
    bool failed=false;try{a.restoreRandomState(encoded.str());}catch(const std::invalid_argument&){failed=true;}check(failed,"RNG restore cannot overwrite an initialized workspace.");
    for(const auto& text:{std::string("-1 ")+encoded.str(),std::string(32769,'1'),std::string("0 ")+encoded.str(),encoded.str()+" 1"}){penta::runtime::InterpreterSession session;failed=false;try{session.restoreRandomState(text);}catch(const std::invalid_argument&){failed=true;}check(failed,"Malformed or oversized RNG state is rejected.");}
    // The only files removed here are this test's exact newly created directory.
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);std::cout<<checks<<" callback snapshot/replay checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
