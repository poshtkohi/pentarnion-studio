#include "runtime/interpreter.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(){
    namespace fs=std::filesystem;
    const fs::path sourceRoot=PENTA_SOURCE_DIR;
    const auto temporary=fs::temp_directory_path()/("penta-roadmap2-examples-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int failures=0,checks=0;
    const auto read=[](const fs::path& path){std::ifstream f(path,std::ios::binary);
        if(!f)throw std::runtime_error("Cannot read example: "+path.string());
        return std::string(std::istreambuf_iterator<char>(f),{});};
    try{
        const std::vector<std::string> topics{
            "native-discovery","parallel-pdes","stiff-pdes","pde-patterns",
            "periodic-orbits","differential-geometry","global-sensitivity",
            "native-inference-and-refits","exact-pentarnion-symbolics",
            "implicit-daes-and-hybrid-events","native-mechanics",
            "multidimensional-fields","field-animation","publication-plots",
            "graph-theory","interactive-graphs","portable-code-export",
            "research-explorer","real-geometry","differential-forms",
            "surface-diffusion","parallel-pde-experiments","native-networks",
            "constrained-mechanics","tiered-ode-verification","paper2-spin-geometry",
            "solver-summary-sensitivity","physical-quantities","hybrid-event-sensitivities",
            "high-order-finite-elements","bifurcation-point-refinement","experiment-bundles",
            "native-third-derivatives"};
        fs::create_directories(temporary);
        for(const auto& topic:topics){
            const auto folder=sourceRoot/"examples"/topic;
            const auto project=nlohmann::json::parse(read(folder/"project.pprj"));
            penta::language::AnalysisOptions analysis;
            analysis.sourceDirectory=folder;
            penta::runtime::ExecutionOptions execution;
            execution.dataDirectory=temporary/topic;
            execution.odeCacheDirectory=temporary/"native-cache";
            execution.nativeFigureExport=true;
            fs::create_directories(execution.dataDirectory);
            for(const auto& entry:project.at("sources")){
                const auto name=entry.get<std::string>();
                // Throughput measurements are executed separately with controlled
                // worker settings; keep the example smoke suite deterministic.
                if(name=="benchmark.p")continue;
                const auto source=read(folder/name);
                const auto result=penta::runtime::execute(source,analysis,execution);
                ++checks;
                if(!result.ok()){
                    ++failures;std::cerr<<"FAIL "<<topic<<'/'<<name<<'\n';
                    for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';
                    for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';
                }else std::cout<<topic<<'/'<<name<<": "<<result.operations<<" operations, "
                    <<result.figureActions.size()<<" figure actions\n";
            }
        }
    }catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}
    // Preserve failed outputs for diagnosis; successful tests leave no caches.
    if(!failures){std::error_code ec;fs::remove_all(temporary,ec);}
    else std::cerr<<"Failure artifacts: "<<temporary.string()<<'\n';
    std::cout<<checks<<" roadmap2 examples, "<<failures<<" failures\n";
    return failures?1:0;
}
