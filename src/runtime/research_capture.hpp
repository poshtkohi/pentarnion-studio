#pragma once
#include "language/frontend.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace penta::runtime::detail {
struct ResearchInvocation {
    std::string source,identity,initialRandomState;
    std::vector<language::ModuleDependency> imports;
};
// Data only. A session keeps accepted invocations; loading never executes them.
// Evaluation stages call records locally and commits them only on success.
struct ResearchCapture {
    static constexpr std::size_t maximumSourceBytes=12*1024*1024,maximumCalls=512,maximumCallBytes=16*1024*1024;
    std::vector<ResearchInvocation> invocations;
    nlohmann::json calls=nlohmann::json::array();
    std::size_t sourceBytes=0,callBytes=0;
    bool historyComplete=true,callsComplete=true;
    std::string currentInitialRandomState;
};
inline void appendResearchInvocation(ResearchCapture& capture,std::string_view source,const language::AnalysisResult& analysis,std::string identity={},std::string initialRandomState={})noexcept{
    try{std::size_t bytes=source.size();for(const auto& item:analysis.dependencies)if(item.source)bytes+=item.source->size();if(capture.invocations.size()>=256||bytes>ResearchCapture::maximumSourceBytes-capture.sourceBytes){capture.historyComplete=false;return;}capture.invocations.push_back({std::string(source),std::move(identity),std::move(initialRandomState),analysis.dependencies});capture.sourceBytes+=bytes;}catch(...){capture.historyComplete=false;}
}
}
