#include "continuation_refinement_helpers.inc"
    orbits::PeriodicOptions orbitPeriodicOptions(const ChaosFields* fields,SourceSpan span) {
        orbits::PeriodicOptions settings;
        settings.period=chaosNumber(fields,"period",settings.period,span);
        settings.minPeriod=chaosNumber(fields,"min_period",settings.minPeriod,span);
        settings.maxPeriod=chaosNumber(fields,"max_period",settings.maxPeriod,span);
        settings.tolerance=chaosNumber(fields,"tolerance",1e-7,span);
        settings.stabilityTolerance=chaosNumber(fields,"stability_tolerance",1e-5,span);
        settings.maximumSeconds=chaosNumber(fields,"max_seconds",60,span);
        settings.maximumIterations=static_cast<std::size_t>(chaosInteger(fields,"max_iterations",20,1,100,span));
        settings.sampleStep=chaosNumber(fields,"sample_step",0,span);settings.periodScale=chaosNumber(fields,"period_scale",1,span);
        if(const auto* value=chaosOption(fields,"state_scale"))settings.stateScale=numericVector(*value,span,false);
        if(const auto* value=chaosOption(fields,"phase_reference"))settings.phaseReference=numericVector(*value,span,false);
        if(const auto* value=chaosOption(fields,"phase_direction"))settings.phaseDirection=numericVector(*value,span,false);
        auto& solver=settings.solver;solver.t0=chaosNumber(fields,"t0",0,span);
        solver.rtol=chaosNumber(fields,"rtol",1e-9,span);solver.atol=chaosNumber(fields,"atol",1e-11,span);
        solver.maxSteps=static_cast<std::size_t>(chaosInteger(fields,"max_steps",1000000,1,10000000,span));
        solver.maxSamples=std::min<std::size_t>(100000,options_.maxMatrixElements);
        solver.maxRetainedCoordinates=options_.maxMatrixCoordinates;
        solver.sensitivityErrorControl=true;solver.jacobian="analytic";
        const auto method=chaosText(fields,"method","bdf",span);
        if(method!="bdf"&&method!="adams")fail("C2020","Periodic tangent integration supports bdf or adams.",span);
        solver.method=method=="bdf"?ode::Method::Bdf:ode::Method::Adams;
        return settings;
    }
    std::optional<Value> orbitsCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(auto plotted=branchPlotCall(expression,arguments))return plotted;
        if(name!="periodicorbit"&&name!="continueequilibrium"&&name!="continueperiodic")return {};
        const bool branch=name!="periodicorbit",periodic=name!="continueequilibrium";
        std::set<std::string> allowed{"parameters","t0","method","rtol","atol","max_steps","max_seconds","tolerance","stability_tolerance","max_iterations"};
        if(periodic)allowed.insert({"period","min_period","max_period","sample_step","phase_reference","phase_direction","state_scale","period_scale"});
        if(branch)allowed.insert({"parameter","component","points","step","min_step","max_step","min_parameter","max_parameter","auto_refine","refinement","source_experiment","source_run"});
        const auto* fields=chaosFields(arguments,2,allowed,span);
        const auto input=chaosOdeInput(arguments[0],arguments[1],fields,span);const auto n=input.initial.size();
        if(odeSolves_>=std::min<std::size_t>(64,options_.maxOdeSolves))fail("R1005","Native ODE analysis count exceeds the request budget.",span);
        ++odeSolves_;auto native=chaosOdeNativeOptions(span);native.chargeOperations=[this,span](std::size_t work){chargeNumericWork(work,span);};
        auto settings=orbitPeriodicOptions(fields,span);
        try {
            if(!branch){
                const auto result=orbits::periodicOrbit(*input.model,context_.gamma(),input.initial,input.parameters,settings,native,options_.numericModuleDirectory);
                return makeRecord({{"x",numericResult(result.state,1,n,1,span)},
                    {"t",numericResult(result.orbit->times,result.orbit->times.size(),1,1,span)},
                    {"states",numericResult(result.orbit->values,result.orbit->times.size(),n,1,span)},
                    {"monodromy",numericResult(result.monodromy,n,n,1,span)},
                    {"multipliers_real",numericResult(result.multipliersReal,n,1,1,span)},
                    {"multipliers_imag",numericResult(result.multipliersImag,n,1,1,span)},
                    {"multipliers_modulus",numericResult(result.multipliersModulus,n,1,1,span)},
                    {"history",numericResult(result.history,result.history.size()/6,6,1,span)},
                    {"period",result.period},{"closure_residual",result.closureResidual},{"scaled_closure_residual",result.scaledClosureResidual},{"phase_residual",result.phaseResidual},
                    {"neutral_error",result.neutralError},{"amplitude",result.amplitude},{"scaled_amplitude",result.scaledAmplitude},
                    {"converged",I(result.converged)},{"stable",I(result.stable)},{"unstable",I(result.unstable)},
                    {"inconclusive",I(result.inconclusive)},{"neutral_index",I(result.neutralIndex+1)},
                    {"iterations",I(result.iterations)},{"flow_evaluations",I(result.evaluations)},
                    {"steps",I(result.steps)},{"rhs_evaluations",I(result.rhsEvaluations)},{"dimension",I(n)},
                    {"cache_hit",I(result.cacheHit)},{"fundamental_period_verified",I(0)},
                    {"status",result.status},{"message",result.message},{"cache_key",result.key},
                    {"method",chaosText(fields,"method","bdf",span)},{"jacobian",std::string("analytic")}},span);
            }
            const auto selected=chaosText(fields,"parameter","",span);
            if(selected.empty())fail("C2020","Continuation requires parameter=\"declared_parameter_name\" and complete parameter bindings.",span);
            orbits::ContinuationOptions continuation;continuation.periodic=settings;
            continuation.points=static_cast<std::size_t>(chaosInteger(fields,"points",21,2,1000,span));
            continuation.maximumIterations=settings.maximumIterations;continuation.maximumSeconds=settings.maximumSeconds;
            continuation.tolerance=settings.tolerance;continuation.stabilityTolerance=settings.stabilityTolerance;
            continuation.step=chaosNumber(fields,"step",.05,span);
            continuation.minimumStep=chaosNumber(fields,"min_step",.0001,span);
            continuation.maximumStep=chaosNumber(fields,"max_step",std::max(.05,std::abs(continuation.step)),span);
            continuation.minimumParameter=chaosNumber(fields,"min_parameter",-1000000,span);
            continuation.maximumParameter=chaosNumber(fields,"max_parameter",1000000,span);
            bool found=false;std::size_t offset=0;
            for(const auto& parameter:input.model->parameters){
                const auto width=language::coordinateWidth(parameter.type,parameter.schema);
                if(parameter.name==selected){
                    std::size_t component=0;
                    if(width>1){if(!chaosOption(fields,"component"))fail("C2020","Select a complete parameter coefficient explicitly with component=0..width-1.",span);component=static_cast<std::size_t>(chaosInteger(fields,"component",0,0,I(width-1),span));}
                    else if(chaosOption(fields,"component"))fail("C2020","A real parameter does not accept a component option.",span);
                    continuation.parameterIndex=offset+component;found=true;break;
                }
                offset+=width;
            }
            if(!found)fail("C2020","Unknown continuation parameter: "+selected,span);
            const auto width=n+(periodic?1u:0u);
            if(continuation.points*(width+1)>options_.maxMatrixElements||continuation.points*(4*width+20)>options_.maxMatrixCoordinates)fail("C2020","Continuation histories exceed the configured array budget.",span);
            const auto result=periodic?orbits::continuePeriodic(*input.model,context_.gamma(),input.initial,input.parameters,continuation,native,options_.numericModuleDirectory):orbits::continueEquilibrium(*input.model,context_.gamma(),input.initial,input.parameters,continuation,native,options_.numericModuleDirectory);
            std::vector<double> states(result.rows*n);
            for(std::size_t row=0;row<result.rows;++row)std::copy_n(result.states.begin()+static_cast<std::ptrdiff_t>(row*result.dimension),n,states.begin()+static_cast<std::ptrdiff_t>(row*n));
            const auto markers=continuationMarkers(*input.model,input.parameters,result,continuation,fields,native,span);const auto* markerFields=recordFields(markers);
            return makeRecord({{"markers",markers},{"x",numericResult(states,result.rows,n,1,span)},
                {"parameter",numericResult(result.parameters,result.rows,1,1,span)},
                {"tangent",numericResult(result.tangents,result.rows,result.dimension+1,1,span)},
                {"residuals",numericResult(result.residuals,result.rows,1,1,span)},
                {"step_sizes",numericResult(result.stepSizes,result.rows,1,1,span)},
                {"values_real",numericResult(result.valuesReal,result.rows,n,1,span)},
                {"values_imag",numericResult(result.valuesImag,result.rows,n,1,span)},
                {"measure",numericResult(result.measure,result.rows,1,1,span)},
                {"stable",numericResult(result.stable,result.rows,1,1,span)},
                {"unstable",numericResult(result.unstable,result.rows,1,1,span)},
                {"inconclusive",numericResult(result.inconclusive,result.rows,1,1,span)},
                {"fold_candidate",numericResult(result.foldCandidate,result.rows,1,1,span)},
                {"hopf_candidate",numericResult(result.hopfCandidate,result.rows,1,1,span)},
                {"period_doubling_candidate",numericResult(result.periodDoublingCandidate,result.rows,1,1,span)},
                {"stability_change",numericResult(result.stabilityChange,result.rows,1,1,span)},
                {"period",numericResult(result.periods,result.periods.size(),1,1,span)},
                {"closure_residuals",numericResult(result.closureResiduals,result.closureResiduals.size(),1,1,span)},
                {"neutral_errors",numericResult(result.neutralErrors,result.neutralErrors.size(),1,1,span)},
                {"rows",I(result.rows)},{"dimension",I(result.dimension)},{"physical_dimension",I(n)},
                {"evaluations",I(result.evaluations)},{"iterations",I(result.iterations)},{"steps",I(result.steps)},
                {"completed",I(result.completed)},{"cache_hit",I(result.cacheHit)},{"bifurcation_refined",markerFields->at("accepted_count")},
                {"status",result.status},{"message",result.message},{"cache_key",result.key},{"kind",result.kind},
                {"parameter_name",selected},{"parameter_derivative",std::string("analytic")}},span);
        }catch(const orbits::Error& error){fail(error.status==3?"R1006":"C2020",error.what(),span);}
        catch(const ode::OdeError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":error.code,std::string("In native orbit analysis: ")+error.what(),span);}
    }
