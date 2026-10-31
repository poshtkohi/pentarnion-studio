#pragma once
#include "native_ode.hpp"
#include "native_functions/native_callback.hpp"
#include <string>
#include <vector>

namespace penta::ode::hybrid {
struct Options {
    SolveOptions solver;
    std::vector<int> parameterDirections; // Flat zero-based parameters or-1.
    std::vector<double> sensitivityInitial; // Direction-major state seeds.
    double gamma=1,minimumEventInterval=1e-8,grazingTolerance=1e-10;
    std::size_t maximumEvents=100,maximumBytes=128*1024*1024;
    int eventDirection=0;bool terminal=false;
    std::vector<int> eventDirections,eventTerminals,eventPriorities;
    std::string simultaneousPolicy="priority"; // priority winner, or all in priority order.
    double eventTimeTolerance=1e-8,eventGradientTolerance=1e-8;
    bool derivativesRequired=true;
    std::shared_ptr<const language::FunctionDefinition> runningObjective,eventObjective;
};
struct Result {
    std::vector<double> times,values,derivatives,sensitivities;
    std::vector<double> segmentIds,sides,eventTimes,eventBefore,eventAfter,eventTimeDerivatives;
    std::vector<double> eventFixedBefore,eventFixedAfter,eventTotalBefore,eventTotalAfter;
    std::vector<double> saltation,parameterJumps,eventDenominators,transversality;
    std::vector<double> terminalTimeDerivatives,terminalValueDerivatives;
    std::size_t dimension=0,parameters=0,directions=0,segments=1;
    std::size_t steps=0,evaluations=0,sensitivityEvaluations=0,eventEvaluations=0;
    bool terminal=false,cacheHit=false;double elapsedSeconds=0;
    std::string key,eventKey,resetKey,rhsDerivativeKey,fingerprint;
    std::vector<double> eventIndices,eventGroupOffsets,eventGroupIndices;
    std::size_t eventComponents=1;
    bool derivativesAvailable=true;
};
// Callback signatures are(real time, declared states..., declared parameters...).
// Events return real scalar/tuple/fixed-vector roots. Reset returns complete
// states, optionally with a leading real one-based event index parameter.
// This is an explicit-ODE path; it never differentiates an IDAS consistency map.
Result solve(const language::OdeDefinition&,const language::FunctionDefinition& event,
    const language::FunctionDefinition& reset,const std::vector<double>& initial,
    const std::vector<double>& parameters,const Options&,const NativeOptions& = {});
// Sample/RHS Hermite within one continuous epoch. At an exact jump, side selects
// the stored pre/post state. No interpolant spans a reset discontinuity.
std::vector<double> evaluate(const Result&,const std::vector<double>& times,bool pre=false);
struct AdjointResult {
    Result trajectory;double objective=0,elapsedSeconds=0;
    std::vector<double> initialGradient,parameterGradient,eventBefore,eventAfter;
    std::size_t backwardSteps=0,replaySteps=0,checkpoints=0;
    std::string objectiveKey,fingerprint;
    double runningCost=0,eventCost=0,terminalTime=0;
    std::string runningKey,eventCostKey;
};
// Genuine checkpointed CVODES reverse integrations over smooth epochs, with
// Xi^T covector jumps and Bp^T*lambda parameter contributions. Native running
// and pre-reset group costs and moving-terminal boundary terms are supported.
// This remains an explicit-ODE adapter, without DAE consistency projection.
AdjointResult adjoint(const language::OdeDefinition&,const language::FunctionDefinition& event,
    const language::FunctionDefinition& reset,const language::FunctionDefinition& objective,
    const std::vector<double>& initial,const std::vector<double>& parameters,
    const Options&,const NativeOptions& = {});
}
