#pragma once
#include "ode/native_ode.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace penta::orbits {
class Error final:public std::runtime_error {public:int status;Error(int code,const std::string& text):std::runtime_error(text),status(code){}};
struct PeriodicOptions {
    double period=6.283185307179586,minPeriod=1e-4,maxPeriod=100000,tolerance=1e-8,stabilityTolerance=1e-5;
    double maximumSeconds=60,sampleStep=0,periodScale=1;std::size_t maximumIterations=20;
    std::vector<double> phaseReference,phaseDirection,stateScale;
    ode::SolveOptions solver;
};
struct PeriodicResult {
    std::vector<double> state,monodromy,multipliersReal,multipliersImag,multipliersModulus,history;
    std::shared_ptr<const ode::Solution> orbit;
    double period=0,closureResidual=0,scaledClosureResidual=0,phaseResidual=0,neutralError=0,amplitude=0,scaledAmplitude=0;
    std::size_t iterations=0,evaluations=0,steps=0,rhsEvaluations=0,neutralIndex=0;
    bool converged=false,stable=false,unstable=false,inconclusive=true,cacheHit=false;
    std::string status,message,key;
};
struct ContinuationOptions {
    std::size_t parameterIndex=0,points=21,maximumIterations=20;
    double step=.05,minimumStep=.0001,maximumStep=.05,minimumParameter=-1000000,maximumParameter=1000000;
    double tolerance=1e-8,stabilityTolerance=1e-6,maximumSeconds=60;
    PeriodicOptions periodic;
};
struct ContinuationResult {
    std::vector<double> states,parameters,tangents,residuals,stepSizes,valuesReal,valuesImag,measure,stable,unstable,inconclusive,foldCandidate,hopfCandidate,periodDoublingCandidate,stabilityChange,periods,closureResiduals,neutralErrors;
    std::size_t rows=0,dimension=0,physicalDimension=0,evaluations=0,iterations=0,steps=0;
    bool completed=false,cacheHit=false;std::string status,message,key,kind;
};
bool autonomous(const language::OdeDefinition&);
PeriodicResult periodicOrbit(const language::OdeDefinition&,double gamma,const std::vector<double>& initial,
    const std::vector<double>& parameters,const PeriodicOptions& options = {},const ode::NativeOptions& native = {},
    std::filesystem::path moduleDirectory={});
ContinuationResult continueEquilibrium(const language::OdeDefinition&,double gamma,const std::vector<double>& initial,
    const std::vector<double>& parameters,const ContinuationOptions& options = {},const ode::NativeOptions& native = {},
    std::filesystem::path moduleDirectory={});
ContinuationResult continuePeriodic(const language::OdeDefinition&,double gamma,const std::vector<double>& initial,
    const std::vector<double>& parameters,const ContinuationOptions& options = {},const ode::NativeOptions& native = {},
    std::filesystem::path moduleDirectory={});
}
#include "continuation_refinement.hpp"
