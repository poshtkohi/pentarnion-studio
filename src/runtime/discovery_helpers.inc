    using DiscoveryFields=std::map<std::string,Value>;
    struct DiscoveryDomain {std::vector<double> values;std::size_t width=1,count=0;};
    struct DiscoveryInput {std::shared_ptr<const language::OdeDefinition> model;discovery::Options options;std::size_t coordinates=0,parameterCount=0,initialRows=0,gammaCount=0,combinations=1;bool within=true;std::size_t estimated=0;};
    const DiscoveryFields* discoveryFields(const std::vector<Value>& args,std::size_t required,const std::set<std::string>& allowed,SourceSpan span){
        if(args.size()<required||args.size()>required+1)fail("R2050","Incorrect discovery argument count; see help.",span);
        const auto* fields=args.size()>required?recordFields(args.back()):nullptr;
        if(args.size()>required&&!fields)fail("R2050","Discovery options require a typed record.",span);
        if(fields)for(const auto& [key,value]:*fields){(void)value;if(!allowed.contains(key))fail("R2050","Unknown discovery option: "+key,span);}
        return fields;
    }
    static const std::set<std::string>& discoveryOptionNames(){static const std::set<std::string> names{
        "parameters","gammas","threads","t0","t1","sample_step","method","jacobian","rtol","atol","max_steps","max_samples","max_seconds","max_wall_seconds","max_runs","maximum_bytes","max_native_operations","escape_radius","transient","return_tolerance","equilibrium_tolerance","min_return_time","batch_size","checkpoint","resume","overwrite","positive_parameters","physical_coordinates","live_plot"};return names;}
    ode::SolveOptions discoverySolver(const DiscoveryFields* fields,SourceSpan span){
        ode::SolveOptions s;s.t0=chaosNumber(fields,"t0",0,span);s.t1=chaosNumber(fields,"t1",10,span);s.sampleStep=chaosNumber(fields,"sample_step",.01,span);
        s.rtol=chaosNumber(fields,"rtol",1e-8,span);s.atol=chaosNumber(fields,"atol",1e-10,span);s.maximumWallSeconds=chaosNumber(fields,"max_seconds",60,span);
        s.maxSteps=static_cast<std::size_t>(chaosInteger(fields,"max_steps",1000000,1,100000000,span));s.maxSamples=static_cast<std::size_t>(chaosInteger(fields,"max_samples",100000,2,1000000,span));
        const auto method=chaosText(fields,"method","bdf",span);if(method=="bdf")s.method=ode::Method::Bdf;else if(method=="adams")s.method=ode::Method::Adams;else if(method=="erk")s.method=ode::Method::Erk;else if(method=="esdirk")s.method=ode::Method::Esdirk;else fail("R2050","Discovery method must be bdf/adams/erk/esdirk.",span);
        s.jacobian=chaosText(fields,"jacobian","analytic",span);if(s.jacobian!="analytic"&&s.jacobian!="finite_difference")fail("R2050","Discovery Jacobian must be analytic or finite_difference.",span);
        if(s.t0==s.t1||s.sampleStep<=0||s.rtol<=0||s.rtol>=1||s.atol<=0||s.maximumWallSeconds<=0||s.maximumWallSeconds>3600)fail("R2050","Invalid discovery interval, tolerances or time budget.",span);
        const long double samples=std::ceil(std::abs(static_cast<long double>(s.t1)-s.t0)/s.sampleStep)+1;
        if(!std::isfinite(samples)||samples>s.maxSamples)fail("R2050","Discovery sampling exceeds max_samples.",span);
        s.denseOutput=false;return s;
    }
    Value discoverySettings(const ode::SolveOptions& s,const discovery::Options& o,SourceSpan span){
        const std::string method=s.method==ode::Method::Bdf?"bdf":s.method==ode::Method::Adams?"adams":s.method==ode::Method::Erk?"erk":"esdirk";
        return makeRecord({{"t0",s.t0},{"t1",s.t1},{"sample_step",s.sampleStep},{"rtol",s.rtol},{"atol",s.atol},{"method",method},{"jacobian",s.jacobian},{"max_steps",I(s.maxSteps)},{"max_samples",I(s.maxSamples)},{"max_seconds",s.maximumWallSeconds},{"escape_radius",o.escapeRadius},{"transient",o.transient},{"return_tolerance",o.returnTolerance},{"equilibrium_tolerance",o.equilibriumTolerance},{"min_return_time",o.minReturnTime}},span);
    }
    DiscoveryDomain discoveryDomain(const Value& value,Type type,SourceSpan span,std::shared_ptr<const language::ValueSchema> schema={}){
        DiscoveryDomain result;result.width=language::coordinateWidth(type,schema);if(!result.width)fail("R2050","Search parameters require a complete native coordinate schema.",span);
        const auto* record=recordFields(value);const Value* data=&value;
        if(record){const auto kind=record->find("kind"),values=record->find("values");if(kind==record->end()||values==record->end()||!std::holds_alternative<std::string>(kind->second)||(std::get<std::string>(kind->second)!="set"&&std::get<std::string>(kind->second)!="interval"))fail("R2050","Parameter domains require searchset/searchinterval records.",span);data=&values->second;}
        if(!record){appendNativeCoordinates(*data,language::OdeVariable{"parameter",type,span,schema},result.values,span);result.count=1;return result;}
        const auto a=numericArray(*data,span);if(a.values.empty())fail("R2050","A search domain must contain a finite nonempty set.",span);
        if(result.width==1){if(a.components!=1)fail("R2050","Real parameters need real search values.",span);result.count=a.values.size();result.values=a.values;}
        else if(a.components==result.width){result.count=a.rows*a.columns;result.values=a.values;}
        else if(a.columns==result.width){result.count=a.rows;result.values=a.values;}
        else fail("R2050","Search sets require N-by-width complete real coefficient rows or a matching legacy numeric array.",span);
        for(double v:result.values)if(!std::isfinite(v))fail("R2050","Search values must be finite.",span);
        return result;
    }
    DiscoveryInput discoveryInput(const std::vector<Value>& args,const DiscoveryFields* fields,bool plan,SourceSpan span){
        const auto* model=std::get_if<ModelValue>(&args[0]);if(!model||!model->definition)fail("R2050","Discovery needs a declared ODE model.",span);
        DiscoveryInput input;input.model=model->definition;for(const auto& v:input.model->states)input.coordinates+=language::coordinateWidth(v.type,v.schema);
        if(input.coordinates<1||input.coordinates>128)fail("R2050","Discovery currently supports1..128 full real state coordinates.",span);
        const auto points=numericArray(args[1],span);if(points.components!=1||points.columns!=input.coordinates||!points.rows)fail("R2050","Initial rows must match every model coordinate, including all five Pentarnion coefficients.",span);
        input.initialRows=points.rows;const auto* params=chaosOption(fields,"parameters");const auto* bindings=params?recordFields(*params):nullptr;
        if((params&&!bindings)||(bindings?bindings->size():0)!=input.model->parameters.size())fail("R2050","Bind every model parameter exactly once in options(parameters=options(...)).",span);
        std::vector<DiscoveryDomain> domains;
        const auto maxRuns=static_cast<std::size_t>(chaosInteger(fields,"max_runs",4096,1,100000,span));
        std::set<std::string> positive;const auto positiveText=chaosText(fields,"positive_parameters","",span);std::string token;
        auto addPositive=[&]{if(!token.empty()){positive.insert(token);token.clear();}};for(char ch:positiveText){if(ch==','||std::isspace(static_cast<unsigned char>(ch)))addPositive();else token+=ch;}addPositive();
        for(const auto& parameter:input.model->parameters){const auto found=bindings->find(parameter.name);if(found==bindings->end())fail("R2050","Missing model parameter: "+parameter.name,span);auto domain=discoveryDomain(found->second,parameter.type,span,parameter.schema);
            if(positive.erase(parameter.name)){if(parameter.type!=Type::Real)fail("R2050","Positive parameters must be real scalars.",span);for(double v:domain.values)if(v<=0)fail("R2050","Parameter "+parameter.name+" must be strictly positive.",span);}
            if(domain.count>100000000/input.combinations)fail("R2050","Candidate Cartesian product exceeds the preflight counting limit.",span);
            input.combinations*=domain.count;input.parameterCount+=domain.width;domains.push_back(std::move(domain));}
        if(!positive.empty())fail("R2050","positive_parameters names an undeclared parameter.",span);
        std::vector<double> gammas{context_.gamma()};if(const auto* values=chaosOption(fields,"gammas")){if(recordFields(*values)){auto domain=discoveryDomain(*values,Type::Real,span);gammas=std::move(domain.values);}else gammas=numericVector(*values,span);}
        if(gammas.empty())fail("R2050","At least one gamma is required.",span);
        for(double gamma:gammas)if(gamma<0||gamma>1||!std::isfinite(gamma))fail("R2050","Each immutable run gamma must be in[0,1].",span);
        input.gammaCount=gammas.size();if(input.initialRows>100000000/input.combinations||input.gammaCount>100000000/(input.combinations*input.initialRows))fail("R2050","Candidate count exceeds100million preflight limit.",span);
        auto& o=input.options;o.runs=input.initialRows*input.combinations*input.gammaCount;o.physicalCoordinates=static_cast<std::size_t>(chaosInteger(fields,"physical_coordinates",I(input.coordinates),1,I(input.coordinates),span));
        o.threads=static_cast<std::size_t>(chaosInteger(fields,"threads",I(parallel_.settings.threads),0,64,span));o.batchSize=static_cast<std::size_t>(chaosInteger(fields,"batch_size",16,1,4096,span));
        o.solver=discoverySolver(fields,span);o.escapeRadius=chaosNumber(fields,"escape_radius",0,span);o.transient=chaosNumber(fields,"transient",0,span);o.returnTolerance=chaosNumber(fields,"return_tolerance",1e-3,span);o.equilibriumTolerance=chaosNumber(fields,"equilibrium_tolerance",1e-7,span);o.minReturnTime=chaosNumber(fields,"min_return_time",.1,span);o.maximumWallSeconds=chaosNumber(fields,"max_wall_seconds",300,span);
        o.maximumBytes=static_cast<std::size_t>(chaosInteger(fields,"maximum_bytes",256*1024*1024,1024,1024ll*1024*1024,span));o.resume=chaosInteger(fields,"resume",0,0,1,span)!=0;o.overwriteCheckpoint=chaosInteger(fields,"overwrite",0,0,1,span)!=0;
        if(o.escapeRadius<0||o.transient<0||o.transient>=std::abs(o.solver.t1-o.solver.t0)||o.returnTolerance<=0||o.equilibriumTolerance<=0||o.minReturnTime<=0||o.maximumWallSeconds<=0||o.maximumWallSeconds>86400)fail("R2050","Invalid discovery diagnostic/time settings.",span);
        const auto sampleCount=std::ceil(std::abs(static_cast<long double>(o.solver.t1)-o.solver.t0)/o.solver.sampleStep)+1;
        const auto workers=std::min({o.runs,o.batchSize,ThreadBudget::effectiveThreads(o.threads)});
        input.estimated=static_cast<std::size_t>(o.runs*(input.coordinates*16+input.parameterCount*8+9*8+2*8+4+512)+workers*sampleCount*(2*input.coordinates+1)*sizeof(double));
        input.within=o.runs<=maxRuns&&o.runs<=options_.maxMatrixElements/std::max<std::size_t>(1,input.coordinates)&&o.runs<=options_.maxMatrixElements/std::max<std::size_t>(1,input.parameterCount)&&input.estimated<=o.maximumBytes;
        if(!input.within&&!plan)fail("R2050","Discovery exceeds task/memory/array budgets; use discoverplan and choose a bounded sample or explicit limits.",span);
        if(!input.within)return input;
        o.initial.reserve(o.runs*input.coordinates);o.parameters.reserve(o.runs*input.parameterCount);o.gamma.reserve(o.runs);
        for(double gamma:gammas)for(std::size_t combination=0;combination<input.combinations;++combination){std::size_t index=combination;std::vector<double> parameters;for(const auto& domain:domains){const auto selected=index%domain.count;index/=domain.count;parameters.insert(parameters.end(),domain.values.begin()+static_cast<std::ptrdiff_t>(selected*domain.width),domain.values.begin()+static_cast<std::ptrdiff_t>((selected+1)*domain.width));}
            for(std::size_t row=0;row<input.initialRows;++row){o.gamma.push_back(gamma);o.parameters.insert(o.parameters.end(),parameters.begin(),parameters.end());o.initial.insert(o.initial.end(),points.values.begin()+static_cast<std::ptrdiff_t>(row*input.coordinates),points.values.begin()+static_cast<std::ptrdiff_t>((row+1)*input.coordinates));}}
        if(chaosOption(fields,"checkpoint"))o.checkpoint=dataPath(chaosText(fields,"checkpoint","",span),span);
        o.invocationHash=result_.sourceHash;return input;
    }
    Value discoveryReplay(const Expr& expression,const std::vector<Value>& arguments){
        const auto span=expression.span;const bool replay=expression.text=="experimentrun";
        if((!replay&&arguments.size()!=2)||(replay&&arguments.size()!=3&&arguments.size()!=4))fail("R2050","Incorrect experiment replay/status arguments.",span);
        const auto* fields=recordFields(arguments[replay?1:0]);if(!fields)fail("R2050","Expected a completed experiment record.",span);
        auto field=[&](const char* key)->const Value&{const auto found=fields->find(key);if(found==fields->end())fail("R2050",std::string("Experiment is missing field: ")+key,span);return found->second;};
        if(chaosText(fields,"format","",span)!="PentarnionExperiment")fail("R2050","Expected a versioned Pentarnion experiment record.",span);
        const auto runs=static_cast<std::size_t>(integer(field("runs"),span)),n=static_cast<std::size_t>(integer(field("coordinates"),span)),np=static_cast<std::size_t>(integer(field("parameter_count"),span));
        if(runs<1||runs>100000||n<1||n>128||np>4096)fail("R2050","Invalid experiment dimensions.",span);
        const auto id=integer(arguments[replay?2:1],span);if(id<1||static_cast<std::uint64_t>(id)>runs)fail("R2050","Experiment run ID is outside the one-based result range.",span);const auto row=static_cast<std::size_t>(id-1);
        auto matrix=[&](const char* key,std::size_t columns){auto a=numericArray(field(key),span);if(a.components!=1||a.values.size()!=runs*columns||(columns&&a.columns!=columns))fail("R2050",std::string("Invalid experiment matrix: ")+key,span);return a.values;};
        const auto initials=matrix("initials",n),parameters=matrix("parameters",np),gammas=matrix("gammas",1);
        std::vector<double> initial(initials.begin()+static_cast<std::ptrdiff_t>(row*n),initials.begin()+static_cast<std::ptrdiff_t>((row+1)*n));
        std::vector<double> params(parameters.begin()+static_cast<std::ptrdiff_t>(row*np),parameters.begin()+static_cast<std::ptrdiff_t>((row+1)*np));
        const double gamma=gammas[row];if(!std::isfinite(gamma)||gamma<0||gamma>1)fail("R2050","Invalid experiment algebra context.",span);
        auto rowValue=[&](const char* key){return matrix(key,1)[row];};
        if(!replay){
            const auto code=rowValue("status_codes");if(code<0||code>double(PENTA_DISCOVERY_CANCELLED)||std::floor(code)!=code)fail("R2050","Invalid experiment status code.",span);
            const auto states=matrix("states",n);std::vector<double> state(states.begin()+static_cast<std::ptrdiff_t>(row*n),states.begin()+static_cast<std::ptrdiff_t>((row+1)*n));
            std::string message;const auto all=chaosText(fields,"messages","",span),prefix="Run "+std::to_string(id)+": ";const auto start=all.find(prefix);if(start!=std::string::npos){const auto end=all.find('\n',start);message=all.substr(start+prefix.size(),end==std::string::npos?end:end-start-prefix.size());}
            const auto reason=fields->contains("failure_reason_codes")?static_cast<std::uint32_t>(matrix("failure_reason_codes",1)[row]):static_cast<std::uint32_t>((code==6||code==7)?discovery::classifyFailure(code==7?"O1003":"",message):discovery::FailureReason::None);if(reason>10)fail("R2050","Invalid experiment failure reason.",span);return makeRecord({{"failure_reason_code",I(reason)},{"failure_reason",std::string(discovery::failureReasonName(static_cast<discovery::FailureReason>(reason)))},{"run_id",id},{"status_code",I(code)},{"status",std::string(discovery::statusName(static_cast<std::uint32_t>(code)))},{"message",message},{"steps",I(rowValue("steps"))},{"evaluations",I(rowValue("evaluations"))},{"gamma",gamma},{"stop_time",rowValue("stop_times")},{"maximum_norm",rowValue("maximum_norm")},{"return_error",rowValue("return_error")},{"period",rowValue("period")},{"rhs_norm",rowValue("rhs_norm")},{"initial",numericResult(initial,1,n,1,span)},{"parameters",np?numericResult(params,1,np,1,span):numericResult({},0,0,1,span)},{"state",numericResult(state,1,n,1,span)}},span);
        }
        const auto* modelValue=std::get_if<ModelValue>(&arguments[0]);if(!modelValue||!modelValue->definition)fail("R2050","Replay requires the original declared ODE model.",span);
        const auto* stored=recordFields(field("settings"));if(!stored)fail("R2050","Experiment settings must be a typed record.",span);DiscoveryFields settings=*stored;
        if(arguments.size()==4){std::vector<Value> args{arguments[3]};const auto* overrides=discoveryFields(args,0,{"t0","t1","sample_step","method","jacobian","rtol","atol","max_steps","max_samples","max_seconds","max_native_operations"},span);for(const auto& item:*overrides)settings.insert_or_assign(item.first,item.second);}
        auto solveOptions=discoverySolver(&settings,span);if(n>options_.maxMatrixElements/solveOptions.maxSamples)solveOptions.maxSamples=options_.maxMatrixElements/n;
        solveOptions.maxRetainedCoordinates=options_.maxSolutionCoordinates;
        bool nativeQuotaExceeded=false;
        const auto nativeMaximum=static_cast<std::size_t>(chaosInteger(&settings,"max_native_operations",I(options_.maxNativeOperations),1,I(options_.maxNativeOperations),span));
        try{
            auto native=chaosOdeNativeOptions(span);std::size_t work=0;native.chargeOperations=[&](std::size_t count){if(count>nativeMaximum-work){nativeQuotaExceeded=true;throw std::runtime_error("Replay native operation budget reached.");}work+=count;};
            const ode::PreparedModel model(*modelValue->definition,native);if(model.coordinateCount()!=n||model.parameterCount()!=np||model.key()!=chaosText(fields,"cache_key","",span))fail("R2050","Replay model structure is incompatible with this experiment; the original result was retained.",span);
            const auto solution=model.solve(gamma,initial,params,solveOptions,native);chargeNumericWork(1,span);std::string names,widths;
            for(std::size_t k=0;k<solution->stateNames.size();++k){if(k){names+=",";widths+=",";}names+=solution->stateNames[k];widths+=std::to_string(solution->stateWidths[k]);}
            return makeRecord({{"t",numericResult(solution->times,solution->times.size(),1,1,span)},{"values",numericResult(solution->values,solution->times.size(),n,1,span)},{"gamma",gamma},{"run_id",id},{"coordinates",I(n)},{"steps",I(solution->stats.steps)},{"rhs_evaluations",I(solution->stats.rhsEvaluations)},{"native_operations",I(work)},{"cache_hit",I(model.cacheHit())},{"cache_key",model.key()},{"status",std::string("completed")},{"state_names",names},{"state_widths",widths}},span);
        }catch(const Failure&){throw;}catch(const ode::OdeError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":nativeQuotaExceeded?"R1005":"R2050",error.what(),span);}catch(const std::exception& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":nativeQuotaExceeded?"R1005":"R2050",error.what(),span);}
    }
#include "discovery_export_helpers.inc"
#include "discovery_workflow_helpers.inc"
    std::optional<Value> discoveryCall(const Expr& expression,const std::vector<Value>& arguments){
        const auto& name=expression.text;const auto span=expression.span;
        if(auto workflow=discoveryWorkflow(expression,arguments))return workflow;
        if(name=="exportexperiment")return exportExperiment(expression,arguments);
        if(name!="searchset"&&name!="searchinterval"&&name!="pentdirections"&&name!="discoverplan"&&name!="discover"&&name!="odeensemble"&&name!="discoverstatus"&&name!="experimentrun")return {};
        if(name=="pentdirections"){
            if(!arguments.empty())fail("R2050","pentdirections takes no arguments.",span);
            std::vector<double> values;values.reserve(242*5);
            for(int code=0;code<243;++code){int digits=code;std::array<double,5> row{};double squared=0;for(auto& v:row){v=digits%3-1;digits/=3;squared+=v*v;}if(squared==0)continue;for(double v:row)values.push_back(v/std::sqrt(squared));}
            return numericResult(values,242,5,1,span);
        }
        if(name=="searchset"){
            if(arguments.size()!=1)fail("R2050","searchset requires one finite numeric set.",span);
            const auto a=numericArray(arguments[0],span);if(a.values.empty())fail("R2050","Search sets cannot be empty.",span);
            return makeRecord({{"kind",std::string("set")},{"method",std::string("explicit")},{"values",arguments[0]},{"count",I(a.rows*a.columns)},{"seed",I(0)}},span);
        }
        if(name=="searchinterval"){
            if(arguments.size()!=3)fail("R2050","Continuous intervals require explicit sampling options.",span);
            const double lower=finite(real(arguments[0],span),span),upper=finite(real(arguments[1],span),span);if(upper<lower)fail("R2050","Interval endpoints must be ordered.",span);
            std::vector<Value> settings{arguments[2]};const auto* fields=discoveryFields(settings,0,{"samples","method","seed"},span);
            if(!chaosOption(fields,"samples")||!chaosOption(fields,"method"))fail("R2050","Specify interval samples and method=uniform/linspace.",span);
            const auto samples=static_cast<std::size_t>(chaosInteger(fields,"samples",0,1,100000,span));const auto seed=chaosInteger(fields,"seed",0,0,4294967295LL,span);const auto method=chaosText(fields,"method","",span);if(method!="uniform"&&method!="linspace")fail("R2050","Interval sampling method must be uniform or linspace.",span);
            std::mt19937_64 random(static_cast<std::uint64_t>(seed));std::vector<double> values(samples);for(std::size_t k=0;k<samples;++k){const long double u=method=="uniform"?static_cast<double>(random()>>11)*0x1.0p-53:samples==1?.5L:static_cast<long double>(k)/(samples-1);values[k]=finite(static_cast<double>((1-u)*lower+u*upper),span);}
            return makeRecord({{"kind",std::string("interval")},{"method",method},{"values",numericResult(values,samples,1,1,span)},{"count",I(samples)},{"seed",seed}},span);
        }
        if(name=="discoverstatus"||name=="experimentrun")return discoveryReplay(expression,arguments);
        const auto* fields=discoveryFields(arguments,2,discoveryOptionNames(),span);const bool plan=name=="discoverplan";auto input=discoveryInput(arguments,fields,plan,span);auto& o=input.options;
        std::string states,parameters;for(const auto& v:input.model->states){if(!states.empty())states+=",";states+=v.name;}for(const auto& v:input.model->parameters){if(!parameters.empty())parameters+=",";parameters+=v.name;}
        auto data=[&](const std::vector<double>& values,std::size_t columns){return values.empty()?numericResult({},0,0,1,span):numericResult(values,values.size()/columns,columns,1,span);};
        DiscoveryFields output{{"runs",I(o.runs)},{"coordinates",I(input.coordinates)},{"physical_coordinates",I(o.physicalCoordinates)},{"parameter_count",I(input.parameterCount)},{"initial_rows",I(input.initialRows)},{"gamma_count",I(input.gammaCount)},{"parameter_combinations",I(input.combinations)},{"estimated_bytes",I(input.estimated)},{"within_budget",I(input.within)},{"initials",data(o.initial,input.coordinates)},{"parameters",data(o.parameters,std::max<std::size_t>(1,input.parameterCount))},{"gammas",data(o.gamma,1)},{"status",std::string(plan?"preflight":"pending")},{"model_name",input.model->name},{"state_names",states},{"parameter_names",parameters},{"source_hash",result_.sourceHash},{"settings",discoverySettings(o.solver,o,span)}};
        output["format"]=std::string("PentarnionExperiment");
        if(plan)return makeRecord(std::move(output),span);
        try{
            const auto maximum=static_cast<std::uint64_t>(chaosInteger(fields,"max_native_operations",I(options_.maxNativeOperations),1,I(options_.maxNativeOperations),span));
            auto work=std::make_shared<std::atomic_uint64_t>(0);auto exhausted=std::make_shared<std::atomic_bool>(false);
            o.native=chaosOdeNativeOptions(span);o.native.chargeOperations=[work,exhausted,maximum](std::size_t count){const auto previous=work->fetch_add(count,std::memory_order_relaxed);if(previous>maximum||count>maximum-previous){exhausted->store(true,std::memory_order_relaxed);throw std::runtime_error("Discovery native work budget reached.");}};
            o.stopRequested=[exhausted]{return exhausted->load(std::memory_order_relaxed);};const auto live=scientificLiveObserver(chaosOption(fields,"live_plot"),"Discovery completed runs",1,span);if(live)o.progress=[live](std::size_t completed,std::size_t){const std::array<double,1> values{double(completed)};live(double(completed),values);};
            const ode::PreparedModel model(*input.model,o.native);const auto solved=discovery::Module(options_.numericModuleDirectory).run(model,o);
            if(exhausted->load(std::memory_order_relaxed))fail("R1005","Discovery native operation budget reached; completed checkpoint tasks were retained.",span);
            if(solved.cancelled)fail("R1006","Discovery cancelled; completed checkpoint tasks were retained.",span);
            chargeNumericWork(o.runs,span);std::vector<double> reasons(solved.failureReasons.begin(),solved.failureReasons.end()),codes(solved.status.begin(),solved.status.end()),steps(solved.steps.begin(),solved.steps.end()),evals(solved.evaluations.begin(),solved.evaluations.end());std::size_t failures=0,thresholds=0;std::string messages;bool truncated=false;
            for(std::size_t row=0;row<o.runs;++row){failures+=solved.status[row]==PENTA_DISCOVERY_FAILURE||solved.status[row]==PENTA_DISCOVERY_TIMEOUT;thresholds+=solved.status[row]==PENTA_DISCOVERY_THRESHOLD;if(!solved.messages[row].empty()){const auto line="Run "+std::to_string(row+1)+": "+solved.messages[row]+"\n";if(messages.size()+line.size()<=65536)messages+=line;else truncated=true;}}
            for(const auto& [key,values]:std::initializer_list<std::pair<const char*,const std::vector<double>*>>{{"failure_reason_codes",&reasons},{"states",&solved.states},{"status_codes",&codes},{"stop_times",&solved.stopTimes},{"maximum_norm",&solved.maximumNorm},{"return_error",&solved.returnError},{"period",&solved.period},{"rhs_norm",&solved.rhsNorm},{"steps",&steps},{"evaluations",&evals}})output[key]=data(*values,std::string_view(key)=="states"?input.coordinates:1);
            std::string reasonLabels;for(std::size_t row=0;row<o.runs;++row)reasonLabels+="Run "+std::to_string(row+1)+": "+discovery::failureReasonName(static_cast<discovery::FailureReason>(solved.failureReasons[row]))+"\n";output["failure_reason_labels"]=reasonLabels;output["completed"]=I(solved.completed);output["restored"]=I(solved.restored);output["failures"]=I(failures);output["thresholds"]=I(thresholds);output["requested_threads"]=I(o.threads);output["threads_used"]=I(solved.parallel.threadsUsed);output["cache_hit"]=I(model.cacheHit());output["prepared_models"]=I(1);output["checkpoint_ok"]=I(solved.checkpointOkay);output["messages_truncated"]=I(truncated);output["elapsed_seconds"]=solved.elapsedSeconds;output["cache_key"]=model.key();output["request_hash"]=solved.fingerprint;output["messages"]=messages;output["checkpoint_message"]=solved.checkpointMessage;output["status"]=std::string(!solved.checkpointOkay?"checkpoint_failed":solved.completed<o.runs?"partial":failures?"completed_with_failures":"completed");
            if(!solved.checkpointOkay){const auto index=result_.output.size();emitLine("Warning: numerical results retained, but checkpoint/progress failed: "+solved.checkpointMessage,span);result_.warningOutputLines.push_back(index);}
            return makeRecord(std::move(output),span);
        }catch(const Failure&){throw;}catch(const ode::OdeError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R2050",error.what(),span);}catch(const std::exception& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R2050",error.what(),span);}
    }
