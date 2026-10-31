#include "continuation_refinement.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
namespace penta::orbits {
CandidateRefinementResult refineCandidates(const language::OdeDefinition& definition,double gamma,
    const std::vector<double>& parameters,const ContinuationResult& branch,const ContinuationOptions& continuation,
    const CandidateRefinementOptions& settings,const ode::NativeOptions& native,std::filesystem::path directory){
    if(settings.maximumCandidates<1||settings.maximumCandidates>64||!std::isfinite(settings.maximumSeconds)||settings.maximumSeconds<=0||settings.maximumSeconds>86400||continuation.parameterIndex>=parameters.size())throw std::runtime_error("Invalid bounded automatic refinement settings.");
    const auto n=branch.physicalDimension;if(!n||branch.dimension<n||branch.parameters.size()!=branch.rows||branch.states.size()!=branch.rows*branch.dimension||branch.valuesImag.size()!=branch.rows*n||branch.foldCandidate.size()!=branch.rows||branch.hopfCandidate.size()!=branch.rows||branch.periodDoublingCandidate.size()!=branch.rows)throw std::runtime_error("Automatic refinement requires a complete continuation branch.");
    CandidateRefinementResult output;output.sourceKey=branch.key;const auto start=std::chrono::steady_clock::now();
    for(std::size_t row=1;row<branch.rows;++row)for(std::uint32_t kind=0;kind<3;++kind){
        const bool candidate=kind==PENTA_BIFURCATION_FOLD?branch.foldCandidate[row]!=0:kind==PENTA_BIFURCATION_HOPF?branch.hopfCandidate[row]!=0:branch.periodDoublingCandidate[row]!=0;if(!candidate)continue;
        // A periodic branch fold needs a cycle-fold augmented system, distinct
        // from an equilibrium fold. Preserve the candidate and say so openly.
        RefinedMarker marker;marker.candidateRow=row+1;marker.bracketFirst=row;marker.bracketLast=row+1;marker.kind=kind;marker.parameter=.5*(branch.parameters[row-1]+branch.parameters[row]);marker.state.resize(n);
        for(std::size_t k=0;k<n;++k)marker.state[k]=.5*(branch.states[(row-1)*branch.dimension+k]+branch.states[row*branch.dimension+k]);
        if(output.markers.size()>=settings.maximumCandidates){++output.omitted;continue;}
        if(native.cancellation&&native.cancellation->load())throw Error(3,"Automatic continuation refinement cancelled.");
        const double remaining=settings.maximumSeconds-std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        if(n>8){marker.status="unsupported_dimension";marker.message="Augmented bifurcation refinement supports1..8 complete real coordinates.";}
        else if(branch.kind=="periodic"&&kind==PENTA_BIFURCATION_FOLD){marker.status="cycle_fold_not_refined";marker.message="A fold of cycles requires a cycle-fold corrector; equilibrium-fold equations were not substituted.";}
        else if(remaining<=0){marker.status="wall_time_budget";marker.message="Automatic refinement wall-time budget reached.";}
        else try{
            auto options=settings.refinement;options.kind=kind;options.parameter=continuation.parameterIndex;options.maximumSeconds=std::min(options.maximumSeconds,remaining);options.minimumParameter=continuation.minimumParameter;options.maximumParameter=continuation.maximumParameter;
            if(kind==PENTA_BIFURCATION_HOPF){options.auxiliary=0;for(std::size_t k=0;k<n;++k)options.auxiliary=std::max(options.auxiliary,std::abs(branch.valuesImag[row*n+k]));}
            if(kind==PENTA_BIFURCATION_FLIP){if(branch.periods.size()!=branch.rows)throw std::runtime_error("Periodic continuation is missing orbit periods.");options.auxiliary=.5*(branch.periods[row-1]+branch.periods[row]);options.minimumAuxiliary=continuation.periodic.minPeriod;options.maximumAuxiliary=continuation.periodic.maxPeriod;options.solver=continuation.periodic.solver;}
            auto selected=parameters;selected[continuation.parameterIndex]=marker.parameter;const auto result=bifurcation::refine(definition,gamma,marker.state,selected,options,native,directory);const auto& report=result.report;
            marker.state=result.state;marker.parameter=report.final_parameter;marker.auxiliary=report.final_auxiliary;marker.residual=report.residual;marker.status=report.status;marker.message=report.message;marker.fingerprint=result.fingerprint;
            marker.accepted=report.converged&&report.simple&&report.transverse&&report.step_refined&&(kind!=PENTA_BIFURCATION_FOLD||report.nondegenerate)&&report.other_critical_modes==0&&report.residual<=10*options.tolerance;
            // Suppress duplicate roots from adjacent sample intervals while
            // retaining each source interval and its refinement diagnostics.
            if(marker.accepted)for(const auto& previous:output.markers)if(previous.accepted&&previous.kind==kind&&std::abs(previous.parameter-marker.parameter)<=10*options.tolerance){double distance=0;for(std::size_t k=0;k<n;++k)distance=std::hypot(distance,previous.state[k]-marker.state[k]);if(distance<=100*options.tolerance){marker.accepted=false;marker.status="duplicate_refined_marker";break;}}
        }catch(const std::exception& error){if(native.cancellation&&native.cancellation->load())throw Error(3,"Automatic continuation refinement cancelled.");marker.status="refinement_failed";marker.message=error.what();}
        output.accepted+=marker.accepted;output.markers.push_back(std::move(marker));
    }
    return output;
}
}
