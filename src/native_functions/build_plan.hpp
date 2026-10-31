#pragma once
#include "native_callback.hpp"
#include <string>
#include <vector>

namespace penta::native_functions {
struct BuildReport {
    std::size_t requestedJobs=0,effectiveJobs=1,translationUnits=0;
    std::size_t compilerAttempts=0,cacheHits=0,failedPreparations=0,peakJobs=0,peakCompilerJobs=0;
    double compilationWallSeconds=0;
    bool cancelled=false;
    std::vector<std::string> builtKeys;
};
// Prepare native uses visible to static analysis (including imported modules).
// No callback executes here. Unsupported speculative preparation is left for
// its actual call site to diagnose, preserving normal source control flow.
BuildReport prepareProgram(const language::Program&,const Options& callbacks,
    const ode::NativeOptions& models,std::size_t requestedJobs=0,
    const std::vector<std::shared_ptr<const language::FunctionDefinition>>& availableFunctions={});
}
