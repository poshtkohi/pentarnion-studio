#pragma once
#include "sensitivity_module.hpp"
#include "ode/native_ode.hpp"
#include "native_functions/native_callback.hpp"
#include "pde/pde_module.hpp"
#include "dae/dae_module.hpp"
#include <span>
#include <string>

namespace penta::sensitivity::solver {
enum class Kind {Ode,Map,Pde1,Pde2,Dae};
enum class Summary {Final,Mean,Rms,Minimum,Maximum,SpatialMean,FieldRms,Period,LargestLyapunov,EventTime};
double sampledPeriod(std::span<const double> times,std::span<const double> values,std::size_t coordinates,std::size_t coefficient,double transient,double level);
// Immutable owner-prepared scientific state. Every evaluate() creates its own
// solver/trajectory/reaction scratch; a shared native callback is resident.
struct Model {
    Kind kind=Kind::Ode;Summary summary=Summary::Final;
    std::shared_ptr<const ode::PreparedModel> ode;
    std::shared_ptr<const native_functions::Callback> callback;
    std::shared_ptr<const native_functions::Callback> event;
    std::vector<double> initial,parameters,x,y,left,right,boundary;
    std::vector<double> initialDerivative;std::vector<std::uint8_t> algebraic;
    std::vector<std::size_t> parameterCoordinates;
    std::size_t stateCoordinates=1,reactionParameters=0,coefficient=0,node=0;
    std::size_t mapIterations=100,mapTransient=0,maximumBytes=128*1024*1024;
    double gamma=1,transient=0,maximumSeconds=60;
    double crossingLevel=0,qrStep=.5;std::size_t maximumIntervals=4096,eventIndex=0;
    ode::SolveOptions odeOptions;
    penta_pde_time_request_v1 pde{};penta_pde_grid_request_v1 grid{};
    penta_dae_request_v1 daeOptions{};
    ode::NativeOptions native;
    std::function<void(std::size_t)> charge;
    std::filesystem::path moduleDirectory;
    void validate(std::size_t dimensions)const;
    long double estimatedBufferBytes()const;
    double evaluate(std::span<const double> selected)const;
    std::string fingerprint()const;
    std::string cacheKey()const;
};
struct Result {sensitivity::Result sensitivity;std::string fingerprint;};
Result analyze(const Model&,penta_sensitivity_request_v1,const std::shared_ptr<std::atomic_bool>& cancellation={},std::size_t threads=0);
}
