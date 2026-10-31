#include "build_plan.hpp"
#include "third_callback.hpp"
#include "universal_native.hpp"
#include "ode/native_internal.hpp"
#include "ode/advanced_ode_native.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace penta::native_functions {
namespace {
using Clock=std::chrono::steady_clock;
struct Job {std::shared_ptr<const language::FunctionDefinition> function;
    std::shared_ptr<const language::OdeDefinition> model;bool callback=false,direct=false,third=false;};
class Collector {
    using Definition=std::shared_ptr<const language::FunctionDefinition>;
    struct Scope {std::map<std::string,Definition> functions,aliases;std::map<std::string,std::shared_ptr<const language::Program>> imports;};
    std::set<std::string> seen;
    std::map<std::string,std::vector<Definition>> available;
    static bool compatible(const language::FunctionDefinition& a,const language::FunctionDefinition& b){
        if(a.name!=b.name||a.result!=b.result||a.results!=b.results||a.parameters.size()!=b.parameters.size())return false;
        for(std::size_t n=0;n<a.parameters.size();++n)if(a.parameters[n].name!=b.parameters[n].name||a.parameters[n].type!=b.parameters[n].type)return false;
        return true;
    }
    static Definition moduleFunction(const language::Program& program,const Definition& prototype){
        for(const auto& statement:program.statements)if(statement.function&&compatible(*prototype,*statement.function)&&!statement.function->body.empty())return statement.function;
        return {};
    }
    Definition resolve(const language::ExprPtr& expression,const Scope& scope)const{
        const auto& prototype=expression->function;if(!prototype)return {};
        if(!prototype->body.empty())return prototype;
        if(const auto found=scope.aliases.find(expression->text);found!=scope.aliases.end()&&compatible(*prototype,*found->second))return found->second;
        // Imported namespaces have their own declaration scope; never guess a
        // same-named function from another module's body.
        if(expression->kind==language::ExprKind::Member&&!expression->children.empty()){
            auto base=expression->children.front();std::vector<std::string> path;
            while(base&&base->kind==language::ExprKind::Member&&!base->children.empty()){path.push_back(base->text);base=base->children.front();}
            if(base&&base->kind==language::ExprKind::Reference)if(const auto found=scope.imports.find(base->text);found!=scope.imports.end()){
                auto program=found->second;
                for(auto it=path.rbegin();it!=path.rend()&&program;++it){std::shared_ptr<const language::Program> next;for(const auto& statement:program->statements)if(statement.module&&statement.name==*it){next=statement.module;break;}program=std::move(next);}
                return program?moduleFunction(*program,prototype):Definition{};
            }
        }
        if(const auto found=scope.functions.find(prototype->name);found!=scope.functions.end()&&compatible(*prototype,*found->second))return found->second;
        if(const auto found=available.find(prototype->name);found!=available.end()){
            Definition result;std::string identity;
            for(const auto& candidate:found->second)if(compatible(*prototype,*candidate)){
                // Multiple indistinguishable workspace aliases are safe only
                // when their generated callback bodies are structurally equal.
                try{const auto fingerprint=callbackFingerprint(*candidate);if(result&&identity!=fingerprint)return {};identity=fingerprint;result=candidate;}catch(...){return {};}
            }
            return result;
        }
        return {}; // Builtin/dynamic bodyless signatures stay on demand.
    }
public:
    std::vector<Job> jobs;
    explicit Collector(const std::vector<Definition>& functions){for(const auto& function:functions)if(function&&!function->body.empty())available[function->name].push_back(function);}
    void function(Definition definition,bool callback,bool direct=false,bool third=false){
        if(!definition||definition->body.empty()||!definition->captureNames.empty()||jobs.size()>=256)return;
        try {const auto identity=third?"third:"+thirdFingerprint(*definition):std::string(direct?"direct:":"callback:")+(callback?"ad:":"scalar:")+(callback?callbackFingerprint(*definition):functionFingerprint(*definition));
            if(seen.insert(identity).second)jobs.push_back({std::move(definition),{},callback,direct,third});
        }catch(...){/* Unsupported speculative bodies retain their call-site diagnostic. */}
    }
    void model(std::shared_ptr<const language::OdeDefinition> definition){
        if(!definition||jobs.size()>=256)return;
        try{const auto generated=ode::detail::generate(*definition);const auto identity="model:"+ode::detail::sha256(generated.normalized+generated.source);if(seen.insert(identity).second)jobs.push_back({{},std::move(definition),false,false});}
        catch(...){/* Preserve normal ODE construction diagnostics. */}
    }
    void expression(const language::ExprPtr& e,const Scope& scope,bool callbackContext=false,bool modelContext=false,bool parameterDerivativeContext=false,bool thirdContext=false){
        if(!e)return;
        if(e->kind==language::ExprKind::OdeSolve&&e->ode){
            model(e->ode);bool partials=false;
            for(std::size_t n=0;n<e->argumentNames.size();++n)if(e->argumentNames[n]=="sensitivity_parameters"||(e->argumentNames[n]=="sensitivities"&&e->children[n]->text!="none"))partials=true;
            if(partials)function(std::make_shared<language::FunctionDefinition>(ode::rhsFunction(*e->ode)),true);
        }
        static const std::set<std::string> consumers{"root","rootsolve","fixedpoint","equilibrium","jacobian","gradient","hessian","jvp","vjp","hvp","derivative_check","lsqfit","lsqnonlin","curvefit","minimize","mapiterate","mapdiagram","odeadjoint","odefit","evolve","pdesolve","lyapunov","stability","ensemble",
            "femmixed","femsolve","femsystem","femadapt","femtime","meshadapt","hybridsolve","hybridadjoint","pdesolve2","pdeensemble","pdeensemble2","pdereplay","chartgeometry","geodesic","paramesh","formfield","formpullback","laplacebeltrami","surfaceheat","daesolve","mechanics","constrainedmechanics","ndmap","sensitivity","modelsensitivity","profilefit","fitbootstrap","multistartfit","bootstrap"};
        static const std::set<std::string> modelConsumers{"refinebifurcation","hybridsolve","hybridadjoint","discover","odeensemble","experimentrun","verifyexperiment","verifyrun","periodicorbit","continueequilibrium","continueperiodic","networksolve","modelsensitivity"};
        if(e->function){const auto definition=resolve(e,scope);
            if(thirdContext)function(definition,true,false,true);
            else if(callbackContext||e->kind==language::ExprKind::FunctionHandle||e->kind==language::ExprKind::Lambda)function(definition,true);
            else if(definition&&definition->native&&(e->kind==language::ExprKind::Call||e->kind==language::ExprKind::Invoke))function(definition,!definition->results.empty()||usesUniversalCoordinates(*definition),true);
        }
        const bool solverRefit=e->text=="odemultistartfit"||e->text=="odeprofilefit"||e->text=="odefitbootstrap"||e->text=="pdemultistartfit"||e->text=="pdeprofilefit"||e->text=="pdefitbootstrap";
        const bool odeRefit=e->text=="odemultistartfit"||e->text=="odeprofilefit"||e->text=="odefitbootstrap";
        const bool chaosWorkflow=e->text=="basins"||e->text=="lyapunovscan";
        const bool consumer=consumers.contains(e->text)||e->kind==language::ExprKind::OdeSolve||solverRefit||e->text=="pdeimplicit"||e->text=="pdeconverge"||e->text=="masssolve"||e->text=="mechanicsdae"||e->text=="petscroot"||e->text=="propagateprediction"||e->text=="coveragefit"||e->text=="intrinsicgeometry"||e->text=="tensorcovariant"||e->text=="charttransition"||e->text=="chartatlas"||e->text=="atlasgeometry"||e->text=="atlastransition"||chaosWorkflow;
        if(e->ode&&e->type==language::Type::OdeModel){
            if(modelContext){model(e->ode);if(parameterDerivativeContext)function(std::make_shared<language::FunctionDefinition>(ode::rhsFunction(*e->ode)),true);}
            else if(callbackContext)function(std::make_shared<language::FunctionDefinition>(ode::rhsFunction(*e->ode)),true);
        }
        if(e->text=="mechanicsdae")for(std::size_t n=0;n<std::min<std::size_t>(2,e->children.size());++n)expression(e->children[n],scope,true,false,false,true);
        if(e->text=="formpullback"&&e->children.size()>1)expression(e->children[1],scope,true,false,false,true);
        if((e->text=="daeanalyze"||e->text=="jacobiansparse"||e->text=="daestructure"||e->text=="daeinitialize")&&!e->children.empty())expression(e->children[0],scope,true);
        if(e->text=="lineardaesolve"&&e->children.size()>2){
            expression(e->children[2],scope,true);bool third=true;
            if(e->children.size()>4){const auto& settings=e->children[4];for(std::size_t n=0;n<settings->argumentNames.size()&&n<settings->children.size();++n)if(settings->argumentNames[n]=="maximum_index"&&settings->children[n]->kind==language::ExprKind::IntegerLiteral&&settings->children[n]->text!="3")third=false;}
            if(third)expression(e->children[2],scope,true,false,false,true);
        }
        for(const auto& child:e->children)expression(child,scope,callbackContext||consumer,modelContext||modelConsumers.contains(e->text)||odeRefit||chaosWorkflow,parameterDerivativeContext||odeRefit||e->text=="lyapunovscan"||e->text=="refinebifurcation"||e->text=="hybridsolve"||e->text=="hybridadjoint",thirdContext||e->text=="thirdderivative");
    }
    void statements(const std::vector<language::Statement>& body,const Scope& parent={}){
        if(body.empty())return;
        Scope scope=parent;
        for(const auto& statement:body){if(statement.kind==language::StatementKind::FunctionDefinition&&statement.function)scope.functions[statement.function->name]=statement.function;if(statement.module)scope.imports[statement.name]=statement.module;}
        for(const auto& statement:body){
            if(statement.expression&&statement.expression->type==language::Type::Function&&(statement.kind==language::StatementKind::Declaration||statement.kind==language::StatementKind::Assignment))if(const auto definition=resolve(statement.expression,scope))scope.aliases[statement.name]=definition;
            expression(statement.expression,scope);for(const auto& argument:statement.arguments)expression(argument,scope);statements(statement.body,scope);statements(statement.alternative,scope);if(statement.function)statements(statement.function->body,scope);if(statement.module)statements(statement.module->statements);
        }
    }
};
}
BuildReport prepareProgram(const language::Program& program,const Options& callbackOptions,
    const ode::NativeOptions& modelOptions,std::size_t requestedJobs,
    const std::vector<std::shared_ptr<const language::FunctionDefinition>>& availableFunctions){
    BuildReport report;report.requestedJobs=requestedJobs;
    if(requestedJobs>64)throw ode::OdeError("N2010","Native compile job maximum must be0..64.");
    Collector collector(availableFunctions);collector.statements(program.statements);report.translationUnits=collector.jobs.size();
    // One on-demand unit needs no pool. Empty cache paths retain their normal
    // call-site diagnostics instead of inventing a speculative cache location.
    if(collector.jobs.size()<2||callbackOptions.build.cacheDirectory.empty())return report;
    const auto hardware=std::max(1u,std::thread::hardware_concurrency());
    report.effectiveJobs=std::min({collector.jobs.size(),std::size_t(8),requestedJobs?requestedJobs:std::min<std::size_t>(4,hardware)});
    std::mutex results;std::atomic_size_t next{0},active{0};
    std::vector<std::pair<Clock::time_point,Clock::time_point>> intervals;
    auto finished=[&](double seconds){
        const auto end=Clock::now();const auto start=end-std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
        std::lock_guard lock(results);intervals.emplace_back(start,end);++report.compilerAttempts;
    };
    auto worker=[&]{for(;;){const auto index=next.fetch_add(1);if(index>=collector.jobs.size())return;
        if(modelOptions.cancellation&&modelOptions.cancellation->load())return;
        const auto running=active.fetch_add(1)+1;{std::lock_guard lock(results);report.peakJobs=std::max(report.peakJobs,running);}
        try {
            auto native=modelOptions;native.compilationFinished=finished;native.cacheStatus={};
            std::string key;bool hit=false;
            const auto& job=collector.jobs[index];
            if(job.model){auto binary=ode::detail::compile(ode::detail::generate(*job.model),native);key=binary->key;hit=binary->cacheHit;}
            else{auto options=callbackOptions;options.build.compilationFinished=finished;options.build.cacheStatus={};options.chargeOperations={};
                if(job.direct)options.build.cacheDirectory=options.build.cacheDirectory.parent_path()/"functions";
                if(job.third){ThirdCallback binary(*job.function,options);key=binary.key();hit=binary.cacheHit();}
                else if(job.callback){Callback binary(*job.function,options);key=binary.key();hit=binary.cacheHit();}
                else{auto binary=prepare(*job.function,options);key=std::move(binary.first);hit=binary.second;}}
            std::lock_guard lock(results);if(hit)++report.cacheHits;else report.builtKeys.push_back(std::move(key));
        }catch(...){std::lock_guard lock(results);++report.failedPreparations;
            if(modelOptions.cancellation&&modelOptions.cancellation->load())report.cancelled=true;}
        active.fetch_sub(1);
    }};
    std::vector<std::thread> workers;workers.reserve(report.effectiveJobs);
    try{for(std::size_t n=0;n<report.effectiveJobs;++n)workers.emplace_back(worker);}
    catch(...){for(auto& workerThread:workers)workerThread.join();throw;}
    for(auto& workerThread:workers)workerThread.join();
    if(modelOptions.cancellation&&modelOptions.cancellation->load())report.cancelled=true;
    // tic/toc excludes the UNION of compiler intervals, not their summed CPU
    // durations; overlapping jobs must not subtract the same wall time twice.
    std::sort(intervals.begin(),intervals.end());
    std::vector<std::pair<Clock::time_point,int>> transitions;
    for(const auto& interval:intervals){transitions.emplace_back(interval.first,1);transitions.emplace_back(interval.second,-1);}
    std::sort(transitions.begin(),transitions.end());std::size_t compiling=0;
    for(const auto& transition:transitions){if(transition.second<0){if(compiling)--compiling;}else{++compiling;report.peakCompilerJobs=std::max(report.peakCompilerJobs,compiling);}}
    if(!intervals.empty()){auto first=intervals[0].first,last=intervals[0].second;
        for(std::size_t n=1;n<intervals.size();++n){if(intervals[n].first<=last)last=std::max(last,intervals[n].second);
            else{report.compilationWallSeconds+=std::chrono::duration<double>(last-first).count();first=intervals[n].first;last=intervals[n].second;}}
        report.compilationWallSeconds+=std::chrono::duration<double>(last-first).count();}
    return report;
}
}
