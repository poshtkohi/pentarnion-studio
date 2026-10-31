#pragma once
#include "discovery_module.hpp"
#include "language/frontend.hpp"
#include <string>
#include <vector>

namespace penta::discovery::verification {
struct SpectrumEstimate {std::vector<double> exponents;double drift=0,orthogonality=0;std::size_t intervals=0,steps=0,evaluations=0;};
SpectrumEstimate spectrum(const ode::PreparedModel&,const std::vector<double>& initial,const std::vector<double>& parameters,double gamma,ode::SolveOptions,double transient,double qrStep,std::size_t maximumIntervals,const ode::NativeOptions&,std::filesystem::path moduleDirectory={});
// Leading complete state groups can exclude work integrals only when none of
// their names occurs in a retained equation. The checked expression tree and
// multiplication groups are shared unchanged, never algebraically rewritten.
language::OdeDefinition physicalModel(const language::OdeDefinition&,std::size_t coordinates);
struct Options {
    std::vector<std::size_t> runIds; // Original one-based experiment identities.
    std::vector<double> horizons; // Positive durations from the recorded t0.
    double tightening=.1,minimumObservation=20,qrStep=.5,lyapunovTolerance=.05;
    double minimumPositive=.02,periodicTolerance=1e-8,stabilityTolerance=1e-5;
    std::size_t maximumIntervals=4096,periodicIterations=20;
    bool lyapunov=false,compareMethod=true,refinePeriodic=false;
    double maximumSeconds=300;
    // Atomic completed-tier ledger. An interrupted tier is rerun in full; a
    // completed tier, including its native diagnostics, is never recomputed.
    std::filesystem::path checkpoint;
    bool resume=false,overwriteCheckpoint=false;
    std::function<void(std::size_t,std::size_t)> tierCompleted;
};
struct Row {
    std::size_t runId=0,tierId=0;
    ode::SolveOptions settings;
    std::uint32_t status=0,screenStatus=0;
    std::string label,message,diagnosticStatus;
    double gamma=0,maximumNorm=0,returnError=0,period=0,rhsNorm=0,stopTime=0;
    double largest=0,windowDrift=0,settingDrift=0,tierDrift=0,orthogonality=0;
    double closureResidual=0,neutralError=0;
    bool lyapunovAvailable=false,windowConverged=false,settingsConverged=false;
    bool tierConverged=false,positive=false,periodicConverged=false;
    bool stable=false,unstable=false;
    std::size_t nearZeroExponents=0;
    std::uint32_t failureReason=0;
    bool quasiperiodicCandidate=false;
    std::vector<double> state,exponents,referenceExponents,floquetModuli;
    std::size_t steps=0,evaluations=0,qrIntervals=0;
};
struct Result {
    std::vector<Row> rows;runtime::ParallelStatistics parallel;
    std::string originalKey,physicalKey,fingerprint;
    std::size_t physicalCoordinates=0,completed=0;
    bool cacheHit=false,cancelled=false;
    std::size_t restoredTiers=0;
    bool checkpointOkay=true;
    std::string checkpointMessage;
    double elapsedSeconds=0;
};
// All kernels are prepared on the caller before dispatch. Numerical tasks only
// call resident native equations, SUNDIALS, native QR and shooting modules.
Result run(const language::OdeDefinition&,const discovery::Options& selectedInput,
           const Options&,const ode::NativeOptions&,std::filesystem::path moduleDirectory={});
}
