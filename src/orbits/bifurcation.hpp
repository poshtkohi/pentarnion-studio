#pragma once
#include "modules/bifurcation_api.h"
#include "ode/native_ode.hpp"
#include <filesystem>
#include <vector>
namespace penta::orbits::bifurcation {
struct Options {
    std::uint32_t kind=PENTA_BIFURCATION_FOLD;std::size_t parameter=0,maximumIterations=25;
    double auxiliary=6.283185307179586,minimumParameter=-1e6,maximumParameter=1e6;
    double minimumAuxiliary=1e-4,maximumAuxiliary=1e6,tolerance=1e-7,spectralTolerance=1e-5;
    double transversalityTolerance=1e-6,fdStep=1e-4,parameterStep=1e-4,fdAgreement=1e-3,maximumSeconds=120;
    std::size_t maximumBytes=64*1024*1024;ode::SolveOptions solver;
};
struct Result {
    std::vector<double> state,vectorReal,vectorImag,matrix,spectrumReal,spectrumImag,history;
    penta_bifurcation_request_v1 report{};
    std::size_t steps=0,rhsEvaluations=0;bool cacheHit=false;
    std::string key,derivativeKey,fingerprint;
};
Result refine(const language::OdeDefinition&,double gamma,const std::vector<double>& initial,
    const std::vector<double>& parameters,const Options&,const ode::NativeOptions& = {},std::filesystem::path directory={});
}
