    using ChaosFields=std::map<std::string,Value>;
    const ChaosFields* chaosFields(const std::vector<Value>& arguments,std::size_t required,
                                   const std::set<std::string>& allowed,SourceSpan span) {
        if(arguments.size()<required||arguments.size()>required+1)fail("C2010","Incorrect analysis argument count; see help for this command.",span);
        const auto* fields=arguments.size()>required?recordFields(arguments.back()):nullptr;
        if(arguments.size()>required&&!fields)fail("C2010","Analysis settings require a typed options record.",span);
        if(fields)for(const auto& [key,value]:*fields){(void)value;if(!allowed.contains(key))fail("C2010","Unknown analysis option: "+key,span);}
        return fields;
    }
    const Value* chaosOption(const ChaosFields* fields,const char* key) {
        if(!fields)return nullptr;
        const auto found=fields->find(key);return found==fields->end()?nullptr:&found->second;
    }
    double chaosNumber(const ChaosFields* fields,const char* key,double fallback,SourceSpan span) {
        const auto* value=chaosOption(fields,key);return value?finite(real(*value,span),span):fallback;
    }
    I chaosInteger(const ChaosFields* fields,const char* key,I fallback,I minimum,I maximum,SourceSpan span) {
        const auto* value=chaosOption(fields,key);const auto result=value?integer(*value,span):fallback;
        if(result<minimum||result>maximum)fail("C2010",std::string(key)+" is outside its bounded integer range.",span);
        return result;
    }
    std::string chaosText(const ChaosFields* fields,const char* key,const std::string& fallback,SourceSpan span) {
        const auto* value=chaosOption(fields,key);if(!value)return fallback;const auto* result=std::get_if<std::string>(value);
        if(!result)fail("C2010",std::string(key)+" must be a string.",span);
        return *result;
    }
    struct ChaosOdeInput {
        std::shared_ptr<const language::OdeDefinition> model;
        std::vector<double> initial,parameters;
    };
    ode::NativeOptions chaosOdeNativeOptions(SourceSpan span) {
        if(options_.odeCacheDirectory.empty()&&options_.dataDirectory.empty())fail("C2010","Native ODE analysis requires a project/source cache directory.",span);
        ode::NativeOptions native;native.cacheDirectory=options_.odeCacheDirectory.empty()?options_.dataDirectory/".penta-cache"/"odes":options_.odeCacheDirectory;
        native.cancellation=options_.cancellation;native.cacheStatus=nativeCacheStatus();native.compilationFinished=[this](double seconds){recordCompilation(seconds);};return native;
    }
    ChaosOdeInput chaosOdeInput(const Value& callable,const Value& point,const ChaosFields* fields,SourceSpan span) {
        const auto* model=std::get_if<ModelValue>(&callable);if(!model||!model->definition)fail("C2010","This analysis requires a declared ODE model.",span);
        ChaosOdeInput input;input.model=model->definition;const auto& definition=*input.model;
        auto append=[&](const Value& value,const language::OdeVariable& variable,std::vector<double>& output){appendNativeCoordinates(value,variable,output,span);};
        if(const auto* tuple=std::get_if<TupleValue>(&point)){if((*tuple)->values.size()!=definition.states.size())fail("C2010","Initial state tuple must match the declared ODE states.",span);for(std::size_t k=0;k<definition.states.size();++k)append((*tuple)->values[k],definition.states[k],input.initial);}
        else if(definition.states.size()==1&&(definition.states[0].type==Type::RealMatrix||definition.states[0].type==Type::PentarnionMatrix||definition.states[0].type==Type::ElementMatrix))append(point,definition.states[0],input.initial);
        else if(std::holds_alternative<MatrixValue>(point)||rangeValue(point))input.initial=numericVector(point,span,false);
        else {if(definition.states.size()!=1)fail("C2010","Multiple ODE states require a typed tuple or flat real state vector.",span);append(point,definition.states.front(),input.initial);}
        std::size_t n=0;for(const auto& state:definition.states)n+=language::coordinateWidth(state.type,state.schema);
        if(input.initial.size()!=n||n<1||n>32)fail("C2010","Analysis requires complete ODE states containing 1..32 real coordinates.",span);
        const auto* parameters=chaosOption(fields,"parameters");const auto* bindings=parameters?recordFields(*parameters):nullptr;
        if(parameters&&!bindings)fail("C2010","parameters requires a typed record.",span);
        if((bindings?bindings->size():0)!=definition.parameters.size())fail("C2010","Bind every ODE parameter exactly once in options(parameters=options(...)).",span);
        for(const auto& parameter:definition.parameters){const auto found=bindings->find(parameter.name);if(found==bindings->end())fail("C2010","Missing ODE parameter: "+parameter.name,span);append(found->second,parameter,input.parameters);}
        return input;
    }
    std::optional<Value> chaosCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(name!="lyapunov"&&name!="poincare"&&name!="recurrence"&&name!="stability"&&name!="ensemble")return {};
        try {
            if(name=="poincare") {
                const auto* fields=chaosFields(arguments,1,{"event","transient"},span);
                const auto* stored=std::get_if<SolutionValue>(&arguments[0]);if(!stored)fail("C2010","poincare requires an ODE solution with native localized events.",span);
                const auto& solution=*stored->data;const auto event=chaosInteger(fields,"event",1,1,16,span);
                const double transient=chaosNumber(fields,"transient",0,span);if(transient<0)fail("C2010","Poincare transient duration must be nonnegative.",span);
                const auto n=solution.coordinateCount();std::vector<double> times,states,intervals,directions;
                const double start=solution.times.empty()?0:solution.times.front();
                for(std::size_t row=0;row<solution.eventTimes.size();++row){tick(span);if(solution.eventIndices[row]!=event||std::abs(solution.eventTimes[row]-start)<transient)continue;times.push_back(solution.eventTimes[row]);states.insert(states.end(),solution.eventValues.begin()+static_cast<std::ptrdiff_t>(row*n),solution.eventValues.begin()+static_cast<std::ptrdiff_t>((row+1)*n));directions.push_back(solution.eventDirections[row]);if(times.size()>1)intervals.push_back(std::abs(times.back()-times[times.size()-2]));}
                const auto rows=times.size();return makeRecord({{"t",numericResult(times,rows,1,1,span)},{"states",numericResult(states,rows,n,1,span)},{"intervals",numericResult(intervals,intervals.size(),1,1,span)},{"direction",numericResult(directions,rows,1,1,span)},{"crossings",I(rows)},{"event",event},{"status",std::string(rows?"crossings":"no_crossings")}},span);
            }
            if(name=="recurrence") {
                const auto* fields=chaosFields(arguments,1,{"radius","lag","theiler"},span);
                auto data=numericArray(arguments[0],span);std::size_t rows=data.rows,dimension=data.columns*data.components;
                if(data.rows==1){rows=data.columns;dimension=data.components;}
                if(rows<2||rows>256||dimension<1||dimension>128||rows*rows>options_.maxMatrixElements)fail("C2010","Recurrence requires 2..256 sample rows and 1..128 real coordinates within the matrix budget.",span);
                const auto lag=static_cast<std::size_t>(chaosInteger(fields,"lag",1,1,static_cast<I>(rows-1),span));
                const auto theiler=static_cast<std::size_t>(chaosInteger(fields,"theiler",0,0,static_cast<I>(rows-1),span));
                const double radius=chaosNumber(fields,"radius",1e-3,span);if(radius<=0)fail("C2010","Recurrence radius must be positive.",span);
                chargeNumericWork(rows*rows*dimension,span);const auto result=chaos::Module(options_.numericModuleDirectory).recurrence(data.values,rows,dimension,radius,theiler);
                std::vector<double> distance(rows-lag),returned(rows-lag),indices(rows-lag);
                for(std::size_t row=0;row<rows-lag;++row){double d=0;for(std::size_t k=0;k<dimension;++k)d=std::hypot(d,data.values[row*dimension+k]-data.values[(row+lag)*dimension+k]);distance[row]=d;returned[row]=d<=radius;indices[row]=static_cast<double>(row+1);}
                return makeRecord({{"matrix",numericResult(result.matrix,rows,rows,1,span)},{"distance",numericResult(distance,distance.size(),1,1,span)},{"returned",numericResult(returned,returned.size(),1,1,span)},{"index",numericResult(indices,indices.size(),1,1,span)},{"rate",result.eligible?static_cast<double>(result.recurrent)/static_cast<double>(result.eligible):0.0},{"radius",radius},{"lag",I(lag)},{"theiler",I(theiler)},{"eligible_pairs",I(result.eligible)},{"recurrent_pairs",I(result.recurrent)},{"status",std::string(result.eligible?"sampled":"no_eligible_pairs")}},span);
            }
            if(name=="stability") {
                const auto* fields=chaosFields(arguments,2,{"parameters","time","kind","tolerance","residual_tolerance","method"},span);
                const bool isOde=std::holds_alternative<ModelValue>(arguments[0]);const std::string kind=chaosText(fields,"kind",isOde?"flow":"map",span);
                if(kind!="flow"&&kind!="map")fail("C2010","Stability kind must be flow or map.",span);
                if(isOde&&kind!="flow")fail("C2010","An ODE model has flow stability; pass a map function for discrete stability.",span);
                const double tolerance=chaosNumber(fields,"tolerance",1e-8,span),residualTolerance=chaosNumber(fields,"residual_tolerance",1e-7,span);
                if(tolerance<=0||residualTolerance<=0)fail("C2010","Stability tolerances must be positive.",span);
                ChaosFields forwarded;for(const auto* key:{"parameters","method"})if(const auto* v=chaosOption(fields,key))forwarded.emplace(key,*v);
                if(isOde)if(const auto* v=chaosOption(fields,"time"))forwarded.emplace("time",*v);
                if(!isOde&&chaosOption(fields,"time"))fail("C2010","Function stability accepts time as an explicitly declared fixed parameter, not a time option.",span);
                Expr derivative=expression;derivative.text="jacobian";std::vector<Value> inputs{arguments[0],arguments[1],makeRecord(forwarded,span)};
                auto matrix=isOde?derivativeModelCall(derivative,inputs):derivativeCall(derivative,inputs);
                if(!matrix)fail("C2010","Stability requires a native-compatible map/RHS function or ODE model.",span);
                const auto jacobian=numericArray(*matrix,span);if(jacobian.components!=1||jacobian.rows!=jacobian.columns||jacobian.rows<1||jacobian.rows>32)fail("C2010","Stability requires a square real state-coordinate Jacobian of dimension1..32.",span);
                std::vector<double> residual;
                if(isOde){const auto input=chaosOdeInput(arguments[0],arguments[1],fields,span);residual=ode::evaluateNative(*input.model,context_.gamma(),chaosNumber(fields,"time",0,span),input.initial,input.parameters,chaosOdeNativeOptions(span)).rhs;}
                else {const auto input=derivativeInput(arguments[0],arguments[1],fields,span);native_functions::Callback native(*input.definition,derivativeNativeOptions(span));native_functions::DerivativeRequest request;request.order=0;residual=native.evaluate(context_.gamma(),input.flat,request).values;if(residual.size()!=input.variableCoordinates)fail("C2010","Map/RHS outputs must match the point state coordinates.",span);if(kind=="map")for(std::size_t k=0;k<residual.size();++k)residual[k]-=input.flat[k];}
                double residualNorm=0;for(double v:residual)residualNorm=std::hypot(residualNorm,v);
                Expr eigExpression=expression;eigExpression.text="eig";const auto eigResult=linalgCall(eigExpression,{*matrix});if(!eigResult)fail("C2010","Native eigenvalue backend is unavailable.",span);
                const auto* eigenFields=recordFields(*eigResult);const auto realValues=numericVector(eigenFields->at("values_real"),span),imagValues=numericVector(eigenFields->at("values_imag"),span);
                bool stable=true,unstable=false;std::vector<double> measure;
                for(std::size_t k=0;k<realValues.size();++k){const double value=kind=="flow"?realValues[k]:std::hypot(realValues[k],imagValues[k])-1;measure.push_back(value);stable=stable&&value < -tolerance;unstable=unstable||value>tolerance;}
                const bool equilibrium=residualNorm<=residualTolerance;const std::string status=!equilibrium?"not_equilibrium":unstable?"unstable":stable?"stable":"inconclusive";
                return makeRecord({{"jacobian",*matrix},{"values_real",eigenFields->at("values_real")},{"values_imag",eigenFields->at("values_imag")},{"measure",numericResult(measure,measure.size(),1,1,span)},{"residual_norm",residualNorm},{"equilibrium",I(equilibrium)},{"stable",I(equilibrium&&stable)},{"unstable",I(equilibrium&&unstable)},{"inconclusive",I(!equilibrium||(!stable&&!unstable))},{"kind",kind},{"status",status}},span);
            }
            if(name=="ensemble")return chaosEnsemble(expression,arguments);
            if(!arguments.empty()&&std::holds_alternative<FunctionValue>(arguments[0]))return chaosMapLyapunov(expression,arguments);
            return chaosLyapunov(expression,arguments);
        }catch(const chaos::Error& error){fail("C2010",error.what(),span);}
        catch(const ode::OdeError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":error.code,std::string("In native dynamical analysis: ")+error.what(),span);}
    }
    Value chaosLyapunov(const Expr& expression,const std::vector<Value>& arguments) {
        const auto span=expression.span;
        const auto* fields=chaosFields(arguments,2,{"parameters","t0","t1","transient","qr_step","rtol","atol","method","max_steps","max_intervals","max_seconds","live_plot"},span);
        const auto input=chaosOdeInput(arguments[0],arguments[1],fields,span);const auto n=input.initial.size();
        ode::SolveOptions settings;settings.t0=chaosNumber(fields,"t0",0,span);settings.t1=chaosNumber(fields,"t1",100,span);
        settings.rtol=chaosNumber(fields,"rtol",1e-8,span);settings.atol=chaosNumber(fields,"atol",1e-10,span);
        const double transient=chaosNumber(fields,"transient",0,span),interval=chaosNumber(fields,"qr_step",0.5,span),maximumSeconds=chaosNumber(fields,"max_seconds",60,span);
        const auto maximumSteps=static_cast<std::size_t>(chaosInteger(fields,"max_steps",1000000,1,10000000,span));
        const auto maximumIntervals=static_cast<std::size_t>(chaosInteger(fields,"max_intervals",4096,1,4096,span));
        const auto method=chaosText(fields,"method","bdf",span);if(method!="bdf"&&method!="adams")fail("C2010","Lyapunov tangent integration currently supports CVODES bdf or adams.",span);settings.method=method=="bdf"?ode::Method::Bdf:ode::Method::Adams;
        if(settings.t1<=settings.t0||interval<=0||transient<0||transient>=settings.t1-settings.t0||settings.rtol<=0||settings.atol<=0||maximumSeconds<=0)fail("C2010","Lyapunov analysis needs increasing time, positive QR interval/tolerances/budget, and 0<=transient<t1-t0.",span);
        const double firstTime=settings.t0,lastTime=settings.t1,accumulateFrom=firstTime+transient;
        if(!(accumulateFrom<lastTime)||(transient>0&&!(accumulateFrom>firstTime)))fail("C2010","The transient boundary must remain a distinct representable time inside the observation interval.",span);
        const long double intervalRatio=(static_cast<long double>(lastTime)-firstTime)/interval;
        if(!std::isfinite(intervalRatio)||intervalRatio>static_cast<long double>(maximumIntervals)+1)fail("C2010","Lyapunov interval count exceeds max_intervals.",span);
        const auto intervals=std::min(maximumIntervals,static_cast<std::size_t>(std::ceil(intervalRatio))+1);
        if(intervals*n>options_.maxMatrixElements||intervals*n>options_.maxMatrixCoordinates/2)fail("C2010","Lyapunov interval/history dimensions exceed their configured budget.",span);
        if(odeSolves_>=std::min<std::size_t>(64,options_.maxOdeSolves))fail("R1005","Native ODE analysis count exceeds the request budget.",span);
        ++odeSolves_;
        auto native=chaosOdeNativeOptions(span);
        native.sampleObserver=scientificLiveObserver(chaosOption(fields,"live_plot"),"Lyapunov state trajectory",n,span);const auto warm=ode::evaluateNative(*input.model,context_.gamma(),firstTime,input.initial,input.parameters,native);
        const auto start=std::chrono::steady_clock::now();settings.denseOutput=false;settings.maxSamples=2;settings.maxRetainedCoordinates=2*(n*n+n*2+1)+64;settings.sensitivityParameters.assign(n,-1);settings.sensitivityErrorControl=true;
        std::vector<double> state=input.initial,basis(n*n,0),sum(n,0),times,history,local;
        for(std::size_t k=0;k<n;++k)basis[k*n+k]=1;
        double current=firstTime,orthogonality=0;std::size_t steps=0,rhs=0,qrCount=0,gridIndex=1;chaos::Module module(options_.numericModuleDirectory);
        while(current<lastTime) {
            tick(span);if(qrCount>=maximumIntervals||steps>=maximumSteps)fail("C2010","Lyapunov analysis exhausted its interval/step budget.",span);
            const long double gridTime=static_cast<long double>(firstTime)+static_cast<long double>(gridIndex)*interval;
            double next=gridTime>=lastTime?lastTime:static_cast<double>(gridTime);
            auto adjacent=[](double a,double b){return std::abs(a-b)<=2*std::numeric_limits<double>::epsilon()*std::max(std::abs(a),std::abs(b));};
            if(adjacent(next,lastTime))next=lastTime;
            if(current<accumulateFrom&&next>accumulateFrom){if(adjacent(next,accumulateFrom))++gridIndex;next=accumulateFrom;}
            else ++gridIndex;
            if(!(next>current))fail("C2010","qr_step cannot advance this physical time.",span);
            settings.t0=current;settings.t1=next;settings.sampleStep=next-current;settings.outputTimes={current,next};settings.maxSteps=maximumSteps-steps;
            settings.maximumWallSeconds=maximumSeconds-std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();if(settings.maximumWallSeconds<=0)fail("C2010","Lyapunov wall-time budget reached.",span);
            settings.sensitivityInitial.resize(n*n);for(std::size_t row=0;row<n;++row)for(std::size_t col=0;col<n;++col)settings.sensitivityInitial[col*n+row]=basis[row*n+col];
            const auto solution=ode::solve(*input.model,context_.gamma(),state,input.parameters,settings,native);
            if(solution->times.size()!=2||solution->sensitivities.size()!=2*n*n)fail("C2010","Native tangent integration returned an unexpected state/sensitivity layout.",span);
            std::copy_n(solution->values.end()-static_cast<std::ptrdiff_t>(n),n,state.begin());
            std::vector<double> tangent(n*n);for(std::size_t row=0;row<n;++row)for(std::size_t col=0;col<n;++col)tangent[row*n+col]=solution->sensitivities[n*n+col*n+row];
            chargeNumericWork(n*n*n,span);auto qr=module.orthonormalize(tangent,n);basis=std::move(qr.basis);orthogonality=std::max(orthogonality,qr.orthogonalityError);++qrCount;
            steps+=solution->stats.steps;rhs+=solution->stats.rhsEvaluations;
            if(current>=accumulateFrom){const double elapsed=next-accumulateFrom;times.push_back(next);for(std::size_t k=0;k<n;++k){sum[k]+=qr.logarithms[k];history.push_back(sum[k]/elapsed);local.push_back(qr.logarithms[k]/(next-current));}}
            current=next;
        }
        const auto rows=times.size();if(rows==0)fail("C2010","Lyapunov analysis retained no post-transient observation intervals.",span);
        std::vector<std::size_t> order(n);std::iota(order.begin(),order.end(),0);std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return sum[a]>sum[b];});
        std::vector<double> exponents(n),sortedHistory(history.size()),sortedLocal(local.size()),windowDifference(n,0);const double duration=lastTime-accumulateFrom;
        for(std::size_t col=0;col<n;++col){exponents[col]=sum[order[col]]/duration;for(std::size_t row=0;row<rows;++row){sortedHistory[row*n+col]=history[row*n+order[col]];sortedLocal[row*n+col]=local[row*n+order[col]];}if(rows>1){const auto half=(rows-1)/2;windowDifference[col]=exponents[col]-history[half*n+order[col]];}}
        return makeRecord({{"exponents",numericResult(exponents,n,1,1,span)},{"largest",exponents.front()},{"t",numericResult(times,rows,1,1,span)},{"history",numericResult(sortedHistory,rows,n,1,span)},{"local_growth",numericResult(sortedLocal,rows,n,1,span)},{"window_difference",numericResult(windowDifference,n,1,1,span)},{"state",numericResult(state,1,n,1,span)},{"orthogonality_error",orthogonality},{"duration",duration},{"transient",transient},{"qr_step",interval},{"intervals",I(qrCount)},{"steps",I(steps)},{"rhs_evaluations",I(rhs)},{"dimension",I(n)},{"cache_hit",I(warm.cacheHit)},{"cache_key",warm.key},{"method",method},{"jacobian",std::string("analytic")},{"status",std::string("finite_time_estimate")},{"message",std::string("Model-based tangent QR estimate; validate duration, transient, QR interval and tolerances before scientific classification.")}},span);
    }
    Value chaosMapLyapunov(const Expr& expression,const std::vector<Value>& arguments) {
        const auto span=expression.span;
        const auto* fields=chaosFields(arguments,2,{"parameters","iterations","transient","qr_stride","max_seconds"},span);
        const auto iterations=static_cast<std::size_t>(chaosInteger(fields,"iterations",1000,1,1000000,span));
        const auto transient=static_cast<std::size_t>(chaosInteger(fields,"transient",0,0,static_cast<I>(iterations-1),span));
        const auto stride=static_cast<std::size_t>(chaosInteger(fields,"qr_stride",1,1,static_cast<I>(iterations),span));
        const double wall=chaosNumber(fields,"max_seconds",60,span);if(wall<=0)fail("C2010","Map Lyapunov wall budget must be positive.",span);
        auto input=derivativeInput(arguments[0],arguments[1],fields,span);const auto n=input.variableCoordinates;
        if(n<1||n>32)fail("C2010","Map Lyapunov analysis requires 1..32 real state coordinates.",span);
        const auto capacity=iterations/stride+3;if(capacity*n>options_.maxMatrixElements||capacity*n>options_.maxMatrixCoordinates/2)fail("C2010","Map Lyapunov history exceeds the configured array budget; increase qr_stride.",span);
        native_functions::Callback native(*input.definition,derivativeNativeOptions(span));
        if(native.outputCount()!=n)fail("C2010","A map must return exactly its initial state coordinates.",span);
        native_functions::DerivativeRequest request;request.order=1;request.requestedThreads=1;request.active.resize(n);std::iota(request.active.begin(),request.active.end(),0);
        const auto started=std::chrono::steady_clock::now();std::vector<double> basis(n*n,0),sum(n,0),times,history,local;for(std::size_t k=0;k<n;++k)basis[k*n+k]=1;
        std::size_t previous=0,intervals=0,evaluations=0;double orthogonality=0;chaos::Module module(options_.numericModuleDirectory);
        ThreadBudget::instance().withReservation([&]{
            for(std::size_t step=1;step<=iterations;++step){
                tick(span);if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>wall)fail("C2010","Map Lyapunov wall-time budget reached.",span);
                const auto value=native.evaluate(context_.gamma(),input.flat,request);evaluations+=value.nativeEvaluations;
                if(value.values.size()!=n||value.jacobian.size()!=n*n)fail("C2010","Native map tangent layout is inconsistent.",span);
                std::vector<double> tangent(n*n,0);chargeNumericWork(n*n*n,span);
                for(std::size_t row=0;row<n;++row)for(std::size_t col=0;col<n;++col){long double v=0;for(std::size_t k=0;k<n;++k)v+=static_cast<long double>(value.jacobian[row*n+k])*basis[k*n+col];tangent[row*n+col]=finite(static_cast<double>(v),span);}
                std::copy(value.values.begin(),value.values.end(),input.flat.begin());basis=std::move(tangent);
                if(step%stride==0||step==transient||step==iterations){
                    auto qr=module.orthonormalize(basis,n);basis=std::move(qr.basis);orthogonality=std::max(orthogonality,qr.orthogonalityError);++intervals;
                    if(previous>=transient){times.push_back(static_cast<double>(step));for(std::size_t k=0;k<n;++k){sum[k]+=qr.logarithms[k];history.push_back(sum[k]/static_cast<double>(step-transient));local.push_back(qr.logarithms[k]/static_cast<double>(step-previous));}}
                    previous=step;
                }
            }
        });
        const auto rows=times.size();if(rows==0)fail("C2010","Map Lyapunov analysis retained no observation intervals.",span);
        std::vector<std::size_t> order(n);std::iota(order.begin(),order.end(),0);std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return sum[a]>sum[b];});
        std::vector<double> exponents(n),sortedHistory(history.size()),sortedLocal(local.size()),windowDifference(n,0);const double duration=static_cast<double>(iterations-transient);
        for(std::size_t col=0;col<n;++col){exponents[col]=sum[order[col]]/duration;for(std::size_t row=0;row<rows;++row){sortedHistory[row*n+col]=history[row*n+order[col]];sortedLocal[row*n+col]=local[row*n+order[col]];}if(rows>1)windowDifference[col]=exponents[col]-history[((rows-1)/2)*n+order[col]];}
        std::vector<double> state(input.flat.begin(),input.flat.begin()+static_cast<std::ptrdiff_t>(n));
        return makeRecord({{"exponents",numericResult(exponents,n,1,1,span)},{"largest",exponents.front()},{"t",numericResult(times,rows,1,1,span)},{"history",numericResult(sortedHistory,rows,n,1,span)},{"local_growth",numericResult(sortedLocal,rows,n,1,span)},{"window_difference",numericResult(windowDifference,n,1,1,span)},{"state",numericResult(state,1,n,1,span)},{"orthogonality_error",orthogonality},{"duration",duration},{"transient",static_cast<double>(transient)},{"qr_step",static_cast<double>(stride)},{"intervals",I(intervals)},{"steps",I(iterations)},{"rhs_evaluations",I(evaluations)},{"dimension",I(n)},{"cache_hit",I(native.cacheHit())},{"cache_key",native.key()},{"method",std::string("map")},{"jacobian",std::string("analytic")},{"status",std::string("finite_time_estimate")},{"message",std::string("Native map tangent QR estimate per iteration; repeat with longer observation, transient and QR strides before classification.")}},span);
    }
    Value chaosEnsemble(const Expr& expression,const std::vector<Value>& arguments) {
        const auto span=expression.span;
        const auto* fields=chaosFields(arguments,2,{"parameters","iterations","threads","escape_radius","max_seconds","max_native_operations","checkpoint","resume","overwrite_checkpoint","retry_failed","batch_size","seed","random_parameter","distribution","ProgressFcn"},span);
        const auto points=numericArray(arguments[1],span);if(points.components!=1||points.rows<1||points.rows>4096||points.columns<1||points.columns>128)fail("C2010","Ensemble initial conditions require1..4096 rows of complete flat real state coordinates (1..128 columns).",span);
        maps::ensemble::Options selected;selected.runs=points.rows;selected.coordinates=points.columns;selected.initial=points.values;selected.iterations=static_cast<std::size_t>(chaosInteger(fields,"iterations",1,1,1000000,span));selected.threads=static_cast<std::size_t>(chaosInteger(fields,"threads",I(parallel_.settings.threads),0,64,span));selected.batchSize=static_cast<std::size_t>(chaosInteger(fields,"batch_size",16,1,256,span));selected.escapeRadius=chaosNumber(fields,"escape_radius",0,span);selected.maximumSeconds=chaosNumber(fields,"max_seconds",60,span);selected.gamma=context_.gamma();selected.seed=static_cast<uint64_t>(chaosInteger(fields,"seed",0,0,std::numeric_limits<I>::max(),span));selected.distribution=chaosText(fields,"distribution","uniform",span);
        selected.resume=chaosInteger(fields,"resume",0,0,1,span)!=0;selected.overwriteCheckpoint=chaosInteger(fields,"overwrite_checkpoint",0,0,1,span)!=0;selected.retryFailed=chaosInteger(fields,"retry_failed",0,0,1,span)!=0;
        if(const auto* path=chaosOption(fields,"checkpoint")){const auto* name=std::get_if<std::string>(path);if(!name)fail("C2010","Map ensemble checkpoint path must be a string.",span);selected.checkpoint=dataPath(*name,span);}else if(selected.resume||selected.overwriteCheckpoint||selected.retryFailed)fail("C2010","Map ensemble resume/overwrite/retry requires a checkpoint path.",span);
        ChaosFields inputFields=fields?*fields:ChaosFields{};const auto randomParameter=chaosText(fields,"random_parameter","",span);
        if(!randomParameter.empty()){
            std::map<std::string,Value> fixed;if(const auto* bindings=chaosOption(fields,"parameters")){const auto* values=recordFields(*bindings);if(!values)fail("C2010","parameters requires a typed record.",span);fixed=*values;}
            if(fixed.contains(randomParameter))fail("C2010","The per-iteration random parameter must not also be bound as a fixed parameter.",span);
            fixed[randomParameter]=0.0;inputFields["parameters"]=makeRecord(std::move(fixed),span);
        }
        const Value first=numericResult(std::vector<double>(points.values.begin(),points.values.begin()+static_cast<std::ptrdiff_t>(points.columns)),1,points.columns,1,span);const auto input=derivativeInput(arguments[0],first,&inputFields,span);requireNativeMapLayout(*input.definition,input.variableCoordinates,span);selected.fixedInput=input.flat;
        if(!randomParameter.empty()){std::size_t offset=0;bool found=false;for(std::size_t n=0;n<input.definition->parameters.size();++n){const auto p=nativeParameter(*input.definition,n,span);if(p.name==randomParameter){if(p.type!=Type::Real||offset<input.variableCoordinates)fail("C2010","random_parameter must name a trailing real callback argument.",span);selected.randomCoordinate=offset;found=true;break;}offset+=language::coordinateWidth(p.type,p.schema);}if(!found)fail("C2010","Unknown native random callback parameter.",span);}
        const auto maximum=static_cast<uint64_t>(chaosInteger(fields,"max_native_operations",I(options_.maxNativeOperations),1,I(options_.maxNativeOperations),span));auto charged=std::make_shared<std::atomic_uint64_t>(0);auto exhausted=std::make_shared<std::atomic_bool>(false);
        auto nativeOptions=derivativeNativeOptions(span);nativeOptions.chargeOperations=[charged,exhausted,maximum](std::size_t count){auto before=charged->load(std::memory_order_relaxed);for(;;){if(before>maximum||count>maximum-before){exhausted->store(true,std::memory_order_relaxed);throw std::runtime_error("Ensemble native operation budget reached.");}if(charged->compare_exchange_weak(before,before+count,std::memory_order_relaxed))return;}};
        native_functions::Callback native(*input.definition,nativeOptions);if(native.outputCount()!=points.columns||input.variableCoordinates!=points.columns)fail("C2010","An ensemble map must return exactly its declared initial state coordinates.",span);selected.callback=&native;selected.cancellation=options_.cancellation;selected.stopRequested=[exhausted]{return exhausted->load(std::memory_order_relaxed);};
        std::optional<native_functions::Callback> monitor;std::size_t progressCalls=0;
        if(const auto* value=chaosOption(fields,"ProgressFcn")){const auto* function=std::get_if<FunctionValue>(value);if(!function||!function->definition||!function->definition->native||function->definition->parameters.size()!=2||function->definition->parameters[0].type!=Type::Real||function->definition->parameters[1].type!=Type::Real||function->definition->result!=Type::Real)fail("C2010","ProgressFcn is a native real function(real completed,real total) returning0 continue or1 clean pause.",span);monitor.emplace(*function->definition,derivativeNativeOptions(span));selected.progress=[&](std::size_t complete,std::size_t total){++progressCalls;std::vector<double> point{static_cast<double>(complete),static_cast<double>(total)};std::array<double,1> out{};monitor->evaluateValues(context_.gamma(),point,out);if(out[0]!=0&&out[0]!=1)fail("C2010","ProgressFcn must return0 or1.",span);return out[0]!=0;};}
        try{
            const auto result=maps::ensemble::run(selected);if(exhausted->load(std::memory_order_relaxed))fail("R1005","Ensemble native operation budget reached; completed checkpoint runs were retained.",span);if(result.cancelled)fail("R1006","Map ensemble cancelled; completed checkpoint runs were retained.",span);
            chargeNumericWork(points.rows,span);std::vector<double> success(points.rows),escaped(points.rows),iterations(points.rows),evaluations(points.rows),status(points.rows);std::size_t failures=0,escapes=0;std::string messages;bool truncated=false;for(std::size_t row=0;row<points.rows;++row){status[row]=result.status[row];success[row]=result.status[row]==1;escaped[row]=result.status[row]==2;iterations[row]=static_cast<double>(result.iterations[row]);evaluations[row]=static_cast<double>(result.evaluations[row]);failures+=result.status[row]==3;escapes+=result.status[row]==2;if(!result.messages[row].empty()){const auto line="Run "+std::to_string(row+1)+": "+result.messages[row]+"\n";if(messages.size()+line.size()<=65536)messages+=line;else truncated=true;}}
            return makeRecord({{"states",numericResult(result.states,points.rows,points.columns,1,span)},{"success",numericResult(success,points.rows,1,1,span)},{"escaped",numericResult(escaped,points.rows,1,1,span)},{"iterations",numericResult(iterations,points.rows,1,1,span)},{"evaluations",numericResult(evaluations,points.rows,1,1,span)},{"task_status",numericResult(status,points.rows,1,1,span)},{"messages",messages},{"messages_truncated",I(truncated)},{"runs",I(points.rows)},{"completed",I(result.completed)},{"restored",I(result.restored)},{"failures",I(failures)},{"escapes",I(escapes)},{"requested_threads",I(selected.threads)},{"threads_used",I(result.parallel.threadsUsed)},{"cache_hit",I(native.cacheHit())},{"cache_key",native.key()},{"request_hash",result.fingerprint},{"checkpoint_ok",I(result.checkpointOkay)},{"checkpoint_message",result.checkpointMessage},{"progress_calls",I(progressCalls)},{"user_stopped",I(result.userStopped)},{"seed",I(selected.seed)},{"random_parameter",randomParameter},{"distribution",selected.distribution},{"rng_protocol",std::string("mt19937_64_splitmix_task_box_muller_v1")},{"elapsed_seconds",result.elapsedSeconds},{"status",std::string(result.userStopped?"progress_function_stop":result.timedOut?"wall_time_budget":result.completed<points.rows||failures||escapes?"partial":"completed")}},span);
        }catch(const Failure&){throw;}catch(const std::exception& error){fail("C2010",error.what(),span);}
    }
