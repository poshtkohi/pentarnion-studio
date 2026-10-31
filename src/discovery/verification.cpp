#include "verification.hpp"
#include "verification_checkpoint.hpp"
#include "chaos/chaos_module.hpp"
#include "orbits/orbits_module.hpp"
#include "ode/native_internal.hpp"
#include "runtime/lazy_module.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>

namespace penta::discovery::verification {
namespace {
using Clock=std::chrono::steady_clock;
void require(bool okay,const char* text){if(!okay)throw std::runtime_error(text);}
bool contains(const language::ExprPtr& expression,const std::set<std::string>& names){
    if(!expression)return false;
    if(expression->kind==language::ExprKind::Reference&&names.contains(expression->text))return true;
    return std::any_of(expression->children.begin(),expression->children.end(),[&](const auto& child){return contains(child,names);});
}
void merge(runtime::ParallelStatistics& total,const runtime::ParallelStatistics& value){
    total.requestedThreads=value.requestedThreads;total.effectiveThreads=std::max(total.effectiveThreads,value.effectiveThreads);
    total.threadsUsed=std::max(total.threadsUsed,value.threadsUsed);total.peakActiveThreads=std::max(total.peakActiveThreads,value.peakActiveThreads);
    total.lastElements=value.lastElements;total.parallelCalls+=value.parallelCalls;total.serialCalls+=value.serialCalls;
}
using Lyapunov=SpectrumEstimate;
Lyapunov tangent(const ode::PreparedModel& model,const std::vector<double>& initial,
        const std::vector<double>& parameters,double gamma,ode::SolveOptions settings,
        double transient,double qrStep,std::size_t maximumIntervals,const ode::NativeOptions& native,
        const chaos::Module& module){
    const auto n=initial.size();const double first=settings.t0,last=settings.t1,observe=first+transient;
    require(last>first&&observe>=first&&observe<last,"Tangent verification requires an increasing observation interval.");
    const long double intervals=std::ceil((static_cast<long double>(last)-first)/qrStep)+1;
    require(std::isfinite(intervals)&&intervals<=maximumIntervals,"Tangent QR interval count exceeds max_intervals.");
    settings.denseOutput=false;settings.maxSamples=2;settings.maxRetainedCoordinates=2*(n*n+2*n+1)+64;
    settings.sensitivityParameters.assign(n,-1);settings.sensitivityErrorControl=true;
    const auto start=Clock::now();const auto maximumSteps=settings.maxSteps;const auto maximumSeconds=settings.maximumWallSeconds;
    std::vector<double> state=initial,basis(n*n,0),sum(n,0),half(n,0);
    for(std::size_t k=0;k<n;++k)basis[k*n+k]=1;
    double time=first,halfTime=0;std::size_t grid=1;Lyapunov result;
    while(time<last){
        require(result.intervals<maximumIntervals&&result.steps<maximumSteps,"Tangent verification exhausted its interval/step budget.");
        if(native.cancellation&&native.cancellation->load(std::memory_order_relaxed))throw ode::OdeError("O1006","Verification cancelled.");
        const long double proposed=static_cast<long double>(first)+static_cast<long double>(grid)*qrStep;
        double next=proposed>=last?last:static_cast<double>(proposed);
        if(time<observe&&next>observe)next=observe;else ++grid;
        require(next>time,"qr_step does not advance the representable physical time.");
        settings.t0=time;settings.t1=next;settings.sampleStep=next-time;settings.outputTimes={time,next};settings.maxSteps=maximumSteps-result.steps;
        settings.maximumWallSeconds=maximumSeconds-std::chrono::duration<double>(Clock::now()-start).count();
        require(settings.maximumWallSeconds>0,"Tangent verification wall-time budget reached.");
        settings.sensitivityInitial.resize(n*n);
        for(std::size_t row=0;row<n;++row)for(std::size_t column=0;column<n;++column)settings.sensitivityInitial[column*n+row]=basis[row*n+column];
        const auto solved=model.solve(gamma,state,parameters,settings,native);
        require(solved->times.size()==2&&solved->sensitivities.size()==2*n*n,"Tangent verification returned an inconsistent sensitivity layout.");
        std::copy_n(solved->values.end()-static_cast<std::ptrdiff_t>(n),n,state.begin());
        std::vector<double> jacobian(n*n);
        for(std::size_t row=0;row<n;++row)for(std::size_t column=0;column<n;++column)jacobian[row*n+column]=solved->sensitivities[n*n+column*n+row];
        if(native.chargeOperations)native.chargeOperations(n*n*n);
        auto qr=module.orthonormalize(jacobian,n);basis=std::move(qr.basis);result.orthogonality=std::max(result.orthogonality,qr.orthogonalityError);
        ++result.intervals;result.steps+=solved->stats.steps;result.evaluations+=solved->stats.rhsEvaluations+solved->stats.sensitivityRhsEvaluations;
        if(time>=observe){for(std::size_t k=0;k<n;++k)sum[k]+=qr.logarithms[k];if(next<=observe+(last-observe)/2){half=sum;halfTime=next-observe;}}
        time=next;
    }
    require(halfTime>0,"Tangent convergence needs at least two post-transient QR windows.");
    const double duration=last-observe;std::vector<std::size_t> order(n);std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return sum[a]>sum[b];});result.exponents.resize(n);
    std::vector<double> firstWindow(n),secondWindow(n);
    for(std::size_t k=0;k<n;++k){result.exponents[k]=sum[order[k]]/duration;firstWindow[k]=half[k]/halfTime;secondWindow[k]=(sum[k]-half[k])/(duration-halfTime);}
    std::sort(firstWindow.begin(),firstWindow.end(),std::greater<>());std::sort(secondWindow.begin(),secondWindow.end(),std::greater<>());
    for(std::size_t k=0;k<n;++k)result.drift=std::max(result.drift,std::abs(firstWindow[k]-secondWindow[k]));
    return result;
}
std::string label(std::uint32_t status){return status==10?"chaotic_candidate":status==11?"quasiperiodic_candidate":discovery::statusName(status);}
}

SpectrumEstimate spectrum(const ode::PreparedModel& model,const std::vector<double>& initial,const std::vector<double>& parameters,double gamma,ode::SolveOptions settings,double transient,double qrStep,std::size_t maximumIntervals,const ode::NativeOptions& native,std::filesystem::path directory){
    require(initial.size()>=1&&initial.size()<=32&&model.coordinateCount()==initial.size()&&model.parameterCount()==parameters.size(),"Native spectrum summary requires complete1..32 state coordinates.");
    require(std::isfinite(qrStep)&&qrStep>0&&maximumIntervals>=2&&maximumIntervals<=16384,"Invalid spectrum summary QR limits.");
    require(settings.method==ode::Method::Bdf||settings.method==ode::Method::Adams,"Native spectrum summaries require CVODES bdf/adams.");
    require(settings.massMatrix.empty()&&settings.eventCount==0,"Lyapunov summaries for mass/event/DAE systems require their correctly differentiated variational flow and are not substituted by explicit ODE tangents.");
    return tangent(model,initial,parameters,gamma,settings,transient,qrStep,maximumIntervals,native,chaos::Module(directory));
}

language::OdeDefinition physicalModel(const language::OdeDefinition& definition,std::size_t coordinates){
    require(!definition.states.empty()&&definition.derivatives.size()==definition.states.size(),"Invalid checked ODE state schema.");
    std::size_t actual=0,groups=0;for(const auto& state:definition.states){if(actual>=coordinates)break;const auto width=language::coordinateWidth(state.type,state.schema);require(width>0,"Physical states require a complete static coordinate schema.");actual+=width;++groups;}
    require(coordinates>0&&actual==coordinates,"physical_coordinates must include leading whole state groups; a Pentarnion cannot be split.");
    std::set<std::string> removed;for(std::size_t k=groups;k<definition.states.size();++k)removed.insert(definition.states[k].name);
    for(std::size_t k=0;k<groups;++k)require(!contains(definition.derivatives[k],removed),"A retained physical equation depends on an excluded auxiliary; select the complete coupled physical state.");
    auto result=definition;result.states.resize(groups);result.derivatives.resize(groups);return result;
}

Result run(const language::OdeDefinition& definition,const discovery::Options& input,const Options& settings,
        const ode::NativeOptions& options,std::filesystem::path directory){
    const auto started=Clock::now();
    require(input.runs>0&&input.runs<=256&&settings.runIds.size()==input.runs,"Verification requires1..256 explicitly selected run IDs.");
    require(!settings.horizons.empty()&&settings.horizons.size()<=8,"Verification requires1..8 explicit horizons.");
    require(input.solver.t1>input.solver.t0,"Verification currently requires an increasing recorded ODE interval.");
    require(std::isfinite(settings.tightening)&&settings.tightening>0&&settings.tightening<=1,"tightening must be in(0,1].");
    require(std::isfinite(settings.maximumSeconds)&&settings.maximumSeconds>0&&settings.maximumSeconds<=86400,"Invalid verification wall-time budget.");
    require(std::isfinite(settings.minimumObservation)&&settings.minimumObservation>0&&std::isfinite(settings.qrStep)&&settings.qrStep>0&&std::isfinite(settings.lyapunovTolerance)&&settings.lyapunovTolerance>0&&std::isfinite(settings.minimumPositive)&&settings.minimumPositive>0,"Invalid tangent verification thresholds.");
    require(settings.maximumIntervals>=2&&settings.maximumIntervals<=16384,"max_intervals must be in2..16384.");
    require(settings.periodicIterations>=1&&settings.periodicIterations<=100&&std::isfinite(settings.periodicTolerance)&&settings.periodicTolerance>0&&std::isfinite(settings.stabilityTolerance)&&settings.stabilityTolerance>0,"Invalid shooting verification thresholds.");
    std::set<std::size_t> identities;for(const auto id:settings.runIds)require(id>0&&identities.insert(id).second,"Verification run IDs must be positive and unique.");
    double previous=input.solver.t1-input.solver.t0;for(std::size_t tier=0;tier<settings.horizons.size();++tier){const double duration=settings.horizons[tier];require(std::isfinite(duration)&&duration>=previous&&(tier==0||duration>previous),"Verification horizons must increase and cannot shorten the original run.");require(input.transient<duration,"Verification transient must fit inside every horizon.");previous=duration;}
    const bool diagnostics=settings.lyapunov||settings.refinePeriodic;
    if(diagnostics)require(input.solver.method==ode::Method::Bdf||input.solver.method==ode::Method::Adams,"Native tangent/shooting verification supports CVODES bdf/adams; select one explicitly.");
    if(settings.lyapunov){for(const double duration:settings.horizons){const auto intervals=std::ceil(static_cast<long double>(duration)/(settings.qrStep/(settings.compareMethod?2:1)))+1;require(std::isfinite(intervals)&&intervals<=settings.maximumIntervals,"Tangent QR interval count exceeds max_intervals.");require(duration-input.transient>2*settings.qrStep,"Tangent convergence requires at least two post-transient QR windows.");}}
    if(options.cancellation&&options.cancellation->load(std::memory_order_relaxed)){Result cancelled;cancelled.cancelled=true;return cancelled;}
    const ode::PreparedModel original(definition,options);const auto n=original.coordinateCount(),np=original.parameterCount(),p=input.physicalCoordinates;
    require(n>=1&&n<=128&&p>=1&&p<=n&&input.initial.size()==input.runs*n&&input.parameters.size()==input.runs*np&&input.gamma.size()==input.runs,"Verification state/parameter input layout is invalid.");
    const auto physical=physicalModel(definition,p);
    if(diagnostics)require(p<=32,"Tangent/shooting verification currently supports1..32 physical coordinates.");
    const ode::PreparedModel physicalPrepared(physical,options);
    const auto autonomous=orbits::autonomous(physical);
    if(settings.refinePeriodic)require(p>=2,"Periodic shooting requires at least two physical coordinates.");
    const long double resultBytes=input.runs*settings.horizons.size()*(2048+8*(n+3*p));
    require(resultBytes<input.maximumBytes,"Verification evidence exceeds maximum_bytes.");
    Result result;result.physicalCoordinates=p;result.originalKey=original.key();result.physicalKey=physicalPrepared.key();result.cacheHit=original.cacheHit();
    result.parallel.threadsUsed=0;result.parallel.peakActiveThreads=0;result.parallel.effectiveThreads=0;
    // Stable textual fingerprint uses exact binary64 spellings and module/backend
    // identities. The original experiment fingerprint is retained by the host.
    std::ostringstream identity;identity.precision(17);identity<<original.key()<<'|'<<physicalPrepared.key()<<'|'<<ode::backendIdentity()<<'|'<<p;
    for(const auto id:settings.runIds)identity<<'|'<<id;
    for(const auto duration:settings.horizons)identity<<'|'<<duration;
    identity<<'|'<<settings.tightening<<'|'<<settings.lyapunov<<'|'<<settings.compareMethod<<'|'<<settings.refinePeriodic<<'|'<<settings.minimumObservation<<'|'<<settings.qrStep<<'|'<<settings.lyapunovTolerance<<'|'<<settings.minimumPositive<<'|'<<settings.maximumIntervals<<'|'<<settings.periodicIterations<<'|'<<settings.periodicTolerance<<'|'<<settings.stabilityTolerance;
    for(const auto* array:{&input.initial,&input.parameters,&input.gamma})for(const double value:*array)identity<<'|'<<value;
    identity<<'|'<<input.solver.t0<<'|'<<input.solver.rtol<<'|'<<input.solver.atol<<'|'<<input.solver.sampleStep<<'|'<<int(input.solver.method)<<'|'<<input.transient;
    identity<<'|'<<input.solver.jacobian<<'|'<<input.solver.maxSteps<<'|'<<input.solver.maxSamples<<'|'<<input.returnTolerance<<'|'<<input.equilibriumTolerance<<'|'<<input.minReturnTime<<'|'<<input.escapeRadius;
    identity<<'|'<<ode::detail::fileHash(runtime::detail::nativeModulePath(directory,"penta_discovery"));
    if(settings.lyapunov)identity<<'|'<<ode::detail::fileHash(runtime::detail::nativeModulePath(directory,"penta_chaos"));
    if(settings.refinePeriodic)identity<<'|'<<ode::detail::fileHash(runtime::detail::nativeModulePath(directory,"penta_orbits"));
    result.fingerprint=ode::detail::sha256("verification_tiers_v3|"+identity.str());
    const auto restored=detail::restoreTiers(result,settings,input,n,p);
    const auto externalCharge=options.chargeOperations;auto native=options;
    native.chargeOperations=[&,externalCharge](std::size_t count){
        if(options.cancellation&&options.cancellation->load(std::memory_order_relaxed))throw ode::OdeError("O1006","Verification cancelled.");
        if(input.stopRequested&&input.stopRequested())throw ode::OdeError("O1006","Verification native work budget reached.");
        if(std::chrono::duration<double>(Clock::now()-started).count()>settings.maximumSeconds)throw ode::OdeError("O1003","Verification wall-time budget reached.");
        if(externalCharge)externalCharge(count);
    };
    const chaos::Module chaos(directory);std::vector<Row> previousRows;
    if(restored)previousRows.assign(result.rows.end()-static_cast<std::ptrdiff_t>(input.runs),result.rows.end());
    for(std::size_t tier=restored;tier<settings.horizons.size();++tier){
        auto runInput=input;runInput.native=native;runInput.solver.t1=input.solver.t0+settings.horizons[tier];runInput.solver.rtol=input.solver.rtol*std::pow(settings.tightening,static_cast<double>(tier));runInput.solver.atol=input.solver.atol*std::pow(settings.tightening,static_cast<double>(tier));
        require(std::isfinite(runInput.solver.t1)&&runInput.solver.t1>input.solver.t0&&runInput.solver.rtol>=1e-14&&runInput.solver.atol>=std::numeric_limits<double>::min(),"A verification horizon/tolerance exceeds its numerical domain.");
        runInput.retainTrajectories=false;runInput.resume=false;runInput.overwriteCheckpoint=false;runInput.checkpoint.clear();
        runInput.maximumWallSeconds=std::min(input.maximumWallSeconds,settings.maximumSeconds-std::chrono::duration<double>(Clock::now()-started).count());
        require(runInput.maximumWallSeconds>0,"Verification wall-time budget reached.");
        runInput.maximumBytes=input.maximumBytes-static_cast<std::size_t>(resultBytes);
        const auto solved=discovery::Module(directory).run(original,runInput);merge(result.parallel,solved.parallel);
        if(solved.cancelled){result.cancelled=true;break;}
        const auto offset=result.rows.size();result.rows.resize(offset+input.runs);
        for(std::size_t k=0;k<input.runs;++k){auto& row=result.rows[offset+k];row.runId=settings.runIds[k];row.tierId=tier+1;row.settings=runInput.solver;row.gamma=input.gamma[k];row.failureReason=solved.failureReasons[k];row.status=solved.status[k];row.screenStatus=row.status;row.label=label(row.status);row.message=solved.messages[k];row.diagnosticStatus=diagnostics?"pending":"not_requested";row.maximumNorm=solved.maximumNorm[k];row.returnError=solved.returnError[k];row.period=solved.period[k];row.rhsNorm=solved.rhsNorm[k];row.stopTime=solved.stopTimes[k];row.steps=solved.steps[k];row.evaluations=solved.evaluations[k];row.state.assign(solved.states.begin()+static_cast<std::ptrdiff_t>(k*n),solved.states.begin()+static_cast<std::ptrdiff_t>((k+1)*n));row.exponents.assign(p,0);row.referenceExponents.assign(p,0);row.floquetModuli.assign(p,0);}
        struct Batch {
            const ode::PreparedModel& model;const language::OdeDefinition& definition;const discovery::Options& input;const Options& settings;
            const ode::NativeOptions& native;const chaos::Module& chaos;Result& result;const std::vector<Row>& previous;
            std::size_t offset,n,np,p;bool autonomous;std::filesystem::path directory;
            static int call(void* raw,uint64_t begin,uint64_t end,char*,uint64_t)noexcept{
                auto& b=*static_cast<Batch*>(raw);
                for(std::size_t k=begin;k<end;++k){auto& row=b.result.rows[b.offset+k];
                    if(b.native.cancellation&&b.native.cancellation->load(std::memory_order_relaxed))return PENTA_NUMERIC_CANCELLED;
                    if(row.status==PENTA_DISCOVERY_FAILURE||row.status==PENTA_DISCOVERY_TIMEOUT||row.status==PENTA_DISCOVERY_THRESHOLD||row.status==PENTA_DISCOVERY_PENDING||row.status==PENTA_DISCOVERY_CANCELLED){row.diagnosticStatus="screen_not_completed";continue;}
                    try{
                        std::vector<double> initial(b.input.initial.begin()+static_cast<std::ptrdiff_t>(k*b.n),b.input.initial.begin()+static_cast<std::ptrdiff_t>(k*b.n+b.p));
                        std::vector<double> parameters(b.input.parameters.begin()+static_cast<std::ptrdiff_t>(k*b.np),b.input.parameters.begin()+static_cast<std::ptrdiff_t>((k+1)*b.np));
                        row.diagnosticStatus="completed";
                        if(b.settings.lyapunov){
                            const auto estimated=tangent(b.model,initial,parameters,row.gamma,row.settings,b.input.transient,b.settings.qrStep,b.settings.maximumIntervals,b.native,b.chaos);
                            row.exponents=estimated.exponents;row.largest=row.exponents.front();row.windowDrift=estimated.drift;row.orthogonality=estimated.orthogonality;row.qrIntervals=estimated.intervals;row.steps+=estimated.steps;row.evaluations+=estimated.evaluations;row.lyapunovAvailable=true;
                            row.windowConverged=row.windowDrift<=b.settings.lyapunovTolerance&&row.settings.t1-row.settings.t0-b.input.transient>=b.settings.minimumObservation&&row.orthogonality<=1e-8;
                            if(b.settings.compareMethod){auto reference=row.settings;reference.method=reference.method==ode::Method::Bdf?ode::Method::Adams:ode::Method::Bdf;reference.rtol=std::max(reference.rtol*.1,1e-14);reference.atol*=.1;
                                const auto repeated=tangent(b.model,initial,parameters,row.gamma,reference,b.input.transient,b.settings.qrStep/2,b.settings.maximumIntervals,b.native,b.chaos);row.referenceExponents=repeated.exponents;row.settingDrift=0;for(std::size_t e=0;e<row.exponents.size();++e)row.settingDrift=std::max(row.settingDrift,std::abs(row.exponents[e]-row.referenceExponents[e]));row.steps+=repeated.steps;row.evaluations+=repeated.evaluations;row.settingsConverged=row.settingDrift<=b.settings.lyapunovTolerance&&repeated.drift<=b.settings.lyapunovTolerance&&repeated.orthogonality<=1e-8;
                            }
                            row.positive=row.largest>b.settings.minimumPositive;
                            row.nearZeroExponents=static_cast<std::size_t>(std::count_if(row.exponents.begin(),row.exponents.end(),[&](double x){return std::abs(x)<=b.settings.minimumPositive;}));
                            if(!b.previous.empty()){const auto& previous=b.previous[k];row.tierDrift=0;for(std::size_t e=0;e<row.exponents.size();++e)row.tierDrift=std::max(row.tierDrift,std::abs(row.exponents[e]-previous.exponents[e]));row.tierConverged=previous.lyapunovAvailable&&previous.windowConverged&&previous.settingsConverged&&row.windowConverged&&row.settingsConverged&&row.tierDrift<=b.settings.lyapunovTolerance;}
                            const bool recurrent=row.screenStatus==PENTA_DISCOVERY_RECURRENT||row.screenStatus==PENTA_DISCOVERY_PERIODIC;
                            const bool previousRecurrent=!b.previous.empty()&&(b.previous[k].screenStatus==PENTA_DISCOVERY_RECURRENT||b.previous[k].screenStatus==PENTA_DISCOVERY_PERIODIC);
                            if(row.positive&&recurrent&&previousRecurrent&&row.windowConverged&&row.settingsConverged&&row.tierConverged){row.status=10;row.label="chaotic_candidate";}
                            else if(b.autonomous&&!row.positive&&row.nearZeroExponents>=2&&recurrent&&previousRecurrent&&row.rhsNorm>b.input.equilibriumTolerance&&row.windowConverged&&row.settingsConverged&&row.tierConverged){row.status=11;row.label="quasiperiodic_candidate";row.quasiperiodicCandidate=true;}
                            else if(row.status!=PENTA_DISCOVERY_EQUILIBRIUM){row.status=PENTA_DISCOVERY_INCONCLUSIVE;row.label="inconclusive";}
                        }
                        if(b.settings.refinePeriodic&&b.autonomous&&row.period>b.input.minReturnTime&&row.returnError<=b.input.returnTolerance){
                            orbits::PeriodicOptions periodic;periodic.period=row.period;periodic.minPeriod=b.input.minReturnTime/2;periodic.maxPeriod=std::max(2*row.period,row.settings.t1-row.settings.t0);periodic.maximumIterations=b.settings.periodicIterations;periodic.tolerance=b.settings.periodicTolerance;periodic.stabilityTolerance=b.settings.stabilityTolerance;periodic.maximumSeconds=row.settings.maximumWallSeconds;periodic.solver=row.settings;periodic.solver.t0=row.stopTime;periodic.sampleStep=row.period/128;
                            std::vector<double> point(row.state.begin(),row.state.begin()+static_cast<std::ptrdiff_t>(b.p));
                            const auto orbit=orbits::periodicOrbit(b.definition,row.gamma,point,parameters,periodic,b.native,b.directory);
                            row.closureResidual=orbit.closureResidual;row.neutralError=orbit.neutralError;row.floquetModuli=orbit.multipliersModulus;row.periodicConverged=orbit.converged&&orbit.closureResidual<=b.settings.periodicTolerance*10&&orbit.neutralError<=b.settings.stabilityTolerance;row.stable=orbit.stable;row.unstable=orbit.unstable;row.steps+=orbit.steps;row.evaluations+=orbit.rhsEvaluations;
                            if(row.periodicConverged){row.period=orbit.period;row.status=PENTA_DISCOVERY_PERIODIC;row.label="periodic_candidate";row.quasiperiodicCandidate=false;}
                            else if(row.status!=10&&row.status!=11&&row.status!=PENTA_DISCOVERY_EQUILIBRIUM){row.status=PENTA_DISCOVERY_INCONCLUSIVE;row.label="inconclusive";}
                            if(!orbit.message.empty())row.message+=(row.message.empty()?"":"; ")+orbit.message;
                        }else if(b.settings.refinePeriodic&&!b.autonomous){row.diagnosticStatus="nonautonomous_shooting_unsupported";row.message+=(row.message.empty()?"":"; ")+std::string("Nonautonomous shooting is unsupported; no Floquet refinement was inferred.");}
                    }catch(const std::exception& error){row.failureReason=static_cast<std::uint32_t>(classifyFailure("",error.what()));row.status=PENTA_DISCOVERY_INCONCLUSIVE;row.label="inconclusive";row.diagnosticStatus="diagnostic_failed";row.message+=(row.message.empty()?"":"; ")+std::string(error.what());}
                    catch(...){row.status=PENTA_DISCOVERY_INCONCLUSIVE;row.label="inconclusive";row.diagnosticStatus="diagnostic_failed";row.message+=(row.message.empty()?"":"; ")+std::string("Native verification diagnostic failed.");}
                }
                return PENTA_NUMERIC_OK;
            }
        };
        if(diagnostics){Batch batch{physicalPrepared,physical,input,settings,native,chaos,result,previousRows,offset,n,np,p,autonomous,directory};runtime::ParallelSettings parallel;parallel.threads=input.threads;parallel.serialThreshold=1;runtime::ParallelStatistics statistics;std::string error;const int status=runtime::ThreadBudget::instance().run(input.runs,1,Batch::call,&batch,parallel,native.cancellation,statistics,error);merge(result.parallel,statistics);if(status==PENTA_NUMERIC_CANCELLED){result.cancelled=true;break;}require(status==0,error.empty()?"Native verification worker failed.":error.c_str());}
        previousRows.assign(result.rows.begin()+static_cast<std::ptrdiff_t>(offset),result.rows.end());
        result.completed+=solved.completed;
        if(solved.completed<input.runs)break;
        detail::saveTiers(result,settings,tier+1);
        if(settings.tierCompleted)settings.tierCompleted(tier+1,settings.horizons.size());
    }
    result.elapsedSeconds=std::chrono::duration<double>(Clock::now()-started).count();return result;
}
}
