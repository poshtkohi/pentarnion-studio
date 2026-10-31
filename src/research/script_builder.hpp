#pragma once
#include "language/frontend.hpp"
#include "spatial_configuration.hpp"
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>

namespace penta::research {
enum class Mode { Ode, Chaos, Fitting, Discovery, Pde, Geometry, Mechanics };
struct State {std::string type="real",name,initial,rhs;};
struct Parameter {std::string type="real",name,value;};
struct AlgebraPreset {std::string name,preset;};
struct ParameterSweep {std::string parameter;double lower=0,upper=1;int samples=3;};
struct Configuration {
    Mode mode=Mode::Ode;double gamma=1,t0=0,t1=10,sampleStep=.02,rtol=1e-8,atol=1e-10,maximumSeconds=60;
    std::string method="adams";std::vector<State> states;std::vector<Parameter> parameters;
    double transient=1,qrStep=.25;int threads=0,maximumIterations=100;
    std::string fitType="real",fitExpression="intercept+slope*t",xData="[0,1,2,3,4]",yData="[1.1,2.8,5.1,7.2,8.8]",fitMethod="lm";
    std::string sweepParameter,initialRows,checkpoint;double sweepLower=1,sweepUpper=4;int sweepSamples=3,maximumRuns=64;bool resume=false,overwriteCheckpoint=false;
    SpatialConfiguration spatial;
    bool livePlots=false;
    std::vector<AlgebraPreset> algebras;
    std::vector<ParameterSweep> sweeps; // Empty retains the original single-parameter controls.
};
class ScriptError final:public std::runtime_error {public:using std::runtime_error::runtime_error;};
Configuration defaultConfiguration(Mode);
std::string prepareAlgebras(const Configuration&,language::AnalysisOptions&);
language::OdeVariable numericControlType(const std::string&,const language::AnalysisOptions&);
// Pure checked generation. This never evaluates expressions, compiles a DLL,
// accesses a source/data file or submits work to Studio's execution worker.
std::string generateScript(const Configuration&,bool preflightOnly=false);
std::string generateSpatialScript(const Configuration&,bool preflightOnly=false);
std::optional<std::string> generateExtendedSpatialScript(const Configuration&,bool preflightOnly=false);
std::string quote(const std::string&);
}
