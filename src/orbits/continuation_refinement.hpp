#pragma once
#include "orbits_module.hpp"
#include "bifurcation.hpp"
namespace penta::orbits {
struct CandidateRefinementOptions {
    std::size_t maximumCandidates=16;
    double maximumSeconds=120;
    bifurcation::Options refinement;
};
struct RefinedMarker {
    std::size_t candidateRow=0,bracketFirst=0,bracketLast=0;
    std::uint32_t kind=0;
    double parameter=0,auxiliary=0,residual=0;
    std::vector<double> state;
    bool accepted=false;
    std::string status,message,fingerprint;
};
struct CandidateRefinementResult {
    std::vector<RefinedMarker> markers;
    std::size_t accepted=0,omitted=0;
    std::string sourceKey;
};
CandidateRefinementResult refineCandidates(const language::OdeDefinition&,double gamma,
    const std::vector<double>& parameters,const ContinuationResult&,
    const ContinuationOptions&,const CandidateRefinementOptions&,
    const ode::NativeOptions& = {},std::filesystem::path directory={});
}
