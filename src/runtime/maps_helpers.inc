    Value mapsPoint(const Value& point,const DerivativeInput& input,const std::vector<double>& coordinates,SourceSpan span) {
        return solverPointResult(coordinates,point,*input.definition,span);
    }
    std::optional<Value> mapsCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(name!="mapiterate"&&name!="mapdiagram"&&name!="returnmap")return {};
        if(name=="returnmap") {
            if(arguments.empty()||arguments.size()>2)fail("R2010","Use returnmap(map_record_or_samples[,options(component=1,lag=1)]).",span);
            const auto* fields=arguments.size()==2?recordFields(arguments[1]):nullptr;
            if(arguments.size()==2&&!fields)fail("R2010","Return-map options must be a typed record.",span);
            if(fields)for(const auto& [key,value]:*fields){(void)value;if(key!="component"&&key!="lag")fail("R2010","Unknown return-map option: "+key,span);}
            I component=1,lag=1;
            if(fields){if(const auto found=fields->find("component");found!=fields->end())component=integer(found->second,span);if(const auto found=fields->find("lag");found!=fields->end())lag=integer(found->second,span);}
            Value samples=arguments[0];std::vector<double> indices;
            const bool record=std::holds_alternative<RecordValue>(samples);
            if(record){const auto* data=recordFields(samples);const auto states=data->find("states"),n=data->find("n");if(states==data->end()||n==data->end()||data->contains("scan_index"))fail("R2010","returnmap accepts one orbit record, not a parameter diagram or unrelated record.",span);samples=states->second;indices=numericVector(n->second,span);}
            auto data=numericArray(samples,span);
            std::size_t rows=data.rows,columns=data.columns*data.components;
            if(!record&&(data.rows==1||data.columns==1)){rows=data.rows*data.columns;columns=data.components;}
            if(component<1||static_cast<std::size_t>(component)>columns||lag<1||static_cast<std::size_t>(lag)>=rows)fail("R2010","Return-map component must select a real coordinate; positive lag must be shorter than the sample count.",span);
            if(!indices.empty()&&indices.size()!=rows)fail("R2010","Map record iteration/sample layout is inconsistent.",span);
            const auto count=rows-static_cast<std::size_t>(lag);std::vector<double> x,y,sourceIndex,targetIndex;x.reserve(count);y.reserve(count);sourceIndex.reserve(count);targetIndex.reserve(count);
            for(std::size_t row=0;row<count;++row){tick(span);const auto next=row+static_cast<std::size_t>(lag);x.push_back(data.values[row*columns+static_cast<std::size_t>(component-1)]);y.push_back(data.values[next*columns+static_cast<std::size_t>(component-1)]);sourceIndex.push_back(indices.empty()?static_cast<double>(row):indices[row]);targetIndex.push_back(indices.empty()?static_cast<double>(next):indices[next]);}
            return makeRecord({{"x",numericResult(x,count,1,1,span)},{"y",numericResult(y,count,1,1,span)},{"source_index",numericResult(sourceIndex,count,1,1,span)},{"target_index",numericResult(targetIndex,count,1,1,span)},{"component",component},{"lag",lag},{"samples",I(count)}},span);
        }
        const bool diagram=name=="mapdiagram";const std::size_t mandatory=diagram?4:2;
        if(arguments.size()<mandatory||arguments.size()>mandatory+1)fail("R2010",diagram?"Use mapdiagram(map,initial,\"parameter\",values[,options]).":"Use mapiterate(map,initial[,options]).",span);
        const auto* fields=arguments.size()>mandatory?recordFields(arguments.back()):nullptr;
        if(arguments.size()>mandatory&&!fields)fail("R2010","Map options must be a typed record.",span);
        const std::set<std::string> allowed{"parameters","iterations","transient","stride","jacobian","escape_radius","max_evaluations","max_wall_seconds","threads"};
        if(fields)for(const auto& [key,value]:*fields){(void)value;if(!allowed.contains(key))fail("R2010","Unknown map option: "+key,span);}
        auto option=[&](const char* key)->const Value*{if(!fields)return nullptr;const auto found=fields->find(key);return found==fields->end()?nullptr:&found->second;};
        auto count=[&](const char* key,I fallback,I minimum,I maximum){const auto* value=option(key);const auto n=value?integer(*value,span):fallback;if(n<minimum||n>maximum)fail("R2010",std::string(key)+" is outside its supported integer bounds.",span);return static_cast<uint64_t>(n);};
        auto number=[&](const char* key,double fallback){const auto* value=option(key);return value?finite(real(*value,span),span):fallback;};
        const auto input=derivativeInput(arguments[0],arguments[1],fields,span);
        requireNativeMapLayout(*input.definition,input.variableCoordinates,span);
        if(input.variableCoordinates<1||input.variableCoordinates>128)fail("R2010","Native maps require 1..128 real state coordinates.",span);
        std::string jacobian="analytic";
        if(const auto* value=option("jacobian")){const auto* text=std::get_if<std::string>(value);if(!text)fail("R2010","Map jacobian must be analytic/ad/automatic, finite_difference or none.",span);jacobian=*text;}
        if(jacobian!="analytic"&&jacobian!="ad"&&jacobian!="automatic"&&jacobian!="finite_difference"&&jacobian!="none")fail("R2010","Map jacobian must be analytic/ad/automatic, finite_difference or none.",span);
        penta_maps_request_v1 request{};request.struct_size=sizeof(request);request.dimension=input.variableCoordinates;
        request.iterations=count("iterations",100,0,1000000);request.transient=count("transient",0,0,1000000);request.stride=count("stride",1,1,1000000);request.final_jacobian=jacobian!="none";
        request.max_evaluations=count("max_evaluations",1004096,1,1004096);request.maximum_wall_seconds=number("max_wall_seconds",60);request.escape_radius=number("escape_radius",0);
        if(request.transient>request.iterations||request.maximum_wall_seconds<=0||request.escape_radius<0)fail("R2010","Map transient must not exceed iterations; wall budget must be positive and escape radius nonnegative.",span);
        const auto requestedThreads=count("threads",static_cast<I>(parallel_.settings.threads),0,64);
        std::vector<double> parameters{0};std::size_t parameterOffset=0;std::string parameterName;
        if(diagram){const auto* parameter=std::get_if<std::string>(&arguments[2]);if(!parameter)fail("R2010","Map diagram parameter must be a declared trailing real parameter name.",span);parameterName=*parameter;parameters=numericVector(arguments[3],span,false);if(parameters.size()>4096)fail("R2010","A map diagram supports at most4096 parameter values.",span);
            bool found=false;std::size_t offset=0;for(std::size_t n=0;n<input.definition->parameters.size();++n){const auto entry=nativeParameter(*input.definition,n,span);if(entry.name==parameterName){if(entry.type!=Type::Real||offset<input.variableCoordinates)fail("R2010","A diagram varies a trailing real runtime parameter, not an initial state or whole-algebra parameter.",span);parameterOffset=offset;found=true;break;}offset+=language::coordinateWidth(entry.type,entry.schema);}if(!found)fail("R2010","Unknown map diagram parameter: "+parameterName,span);
        }
        const auto sampleCapacity=(request.iterations-request.transient)/request.stride+1+((request.iterations-request.transient)%request.stride?1:0);
        if((request.iterations&&parameters.size()>1000000/request.iterations)||sampleCapacity>options_.maxMatrixElements||input.variableCoordinates>options_.maxMatrixElements/sampleCapacity/parameters.size())fail("R2010","Map diagram/retained states exceed the one-million-step or configured matrix-element budget.",span);
        const bool pentOrbit=std::holds_alternative<P>(arguments[1])||(input.variableCoordinates==5&&!std::holds_alternative<MatrixValue>(arguments[1])&&!std::holds_alternative<TupleValue>(arguments[1])&&input.definition->parameters.front().type==Type::Pentarnion);
        try {
            native_functions::Callback native(*input.definition,derivativeNativeOptions(span));
            if(native.outputCount()!=input.variableCoordinates||native.inputCount()!=input.flat.size())fail("R2010","A map must return exactly as many real coordinates as its state arguments.",span);
            struct Bridge {
                const native_functions::Callback* native;double gamma;std::vector<double> input;native_functions::DerivativeRequest derivative;uint64_t nativeEvaluations=0;std::exception_ptr failure;
                static void message(char* output,uint64_t capacity,const char* text)noexcept{if(output&&capacity){uint64_t n=0;while(n+1<capacity&&text[n]){output[n]=text[n];++n;}output[n]=0;}}
                static int evaluate(void* raw,const double* state,uint64_t n,double* values,double* jac,char* error,uint64_t capacity)noexcept{auto& bridge=*static_cast<Bridge*>(raw);try{std::copy_n(state,static_cast<std::size_t>(n),bridge.input.begin());bridge.derivative.order=jac?1u:0u;const auto result=bridge.native->evaluate(bridge.gamma,bridge.input,bridge.derivative);bridge.nativeEvaluations+=result.nativeEvaluations;if(result.values.size()!=n||(jac&&result.jacobian.size()!=n*n))throw std::runtime_error("Invalid native map output/Jacobian layout.");std::copy(result.values.begin(),result.values.end(),values);if(jac)std::copy(result.jacobian.begin(),result.jacobian.end(),jac);return 0;}catch(const std::exception& failure){bridge.failure=std::current_exception();message(error,capacity,failure.what());return PENTA_MAPS_CALLBACK;}catch(...){bridge.failure=std::current_exception();message(error,capacity,"Native map callback failed.");return PENTA_MAPS_CALLBACK;}}
            } bridge{&native,context_.gamma(),input.flat,{},0,{}};
            bridge.derivative.active.resize(input.variableCoordinates);std::iota(bridge.derivative.active.begin(),bridge.derivative.active.end(),0);bridge.derivative.method=jacobian=="finite_difference"?native_functions::DerivativeMethod::FiniteDifference:native_functions::DerivativeMethod::Automatic;bridge.derivative.requestedThreads=static_cast<std::size_t>(requestedThreads);
            request.evaluate=Bridge::evaluate;request.callback_context=&bridge;request.initial=input.flat.data();
            std::vector<double> states,indices,parameterRows,scanIndices,finalStates,escapeFlags,escapeIterations,last,finalJacobian;
            uint64_t iterations=0,evaluations=0,jacobianEvaluations=0,escaped=0;const double wallBudget=request.maximum_wall_seconds;const auto evaluationBudget=request.max_evaluations;const auto started=std::chrono::steady_clock::now();
            maps::Module module(options_.numericModuleDirectory);
            for(std::size_t scan=0;scan<parameters.size();++scan){tick(span);if(diagram)bridge.input[parameterOffset]=parameters[scan];request.maximum_wall_seconds=wallBudget-std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();if(request.maximum_wall_seconds<=0)fail("R2010","Map diagram exceeded max_wall_seconds.",span);if(evaluations>=evaluationBudget)fail("R2010","Map diagram exhausted max_evaluations.",span);request.max_evaluations=evaluationBudget-evaluations;
                maps::Result result;try{result=module.iterate(request,options_.cancellation,[this,span](std::size_t work){chargeNumericWork(work,span);});}catch(...){if(bridge.failure)std::rethrow_exception(bridge.failure);throw;}
                const auto& report=result.report;iterations+=report.iterations;evaluations+=report.evaluations;jacobianEvaluations+=report.jacobian_evaluations;escaped+=report.escaped;states.insert(states.end(),result.states.begin(),result.states.end());indices.insert(indices.end(),result.indices.begin(),result.indices.end());last=std::move(result.solution);finalJacobian=std::move(result.jacobian);finalStates.insert(finalStates.end(),last.begin(),last.end());escapeFlags.push_back(report.escaped);escapeIterations.push_back(static_cast<double>(report.escape_iteration));if(diagram){parameterRows.insert(parameterRows.end(),report.samples,parameters[scan]);scanIndices.insert(scanIndices.end(),report.samples,static_cast<double>(scan+1));}
            }
            const auto samples=indices.size();Value stateValues=numericResult(states,samples,input.variableCoordinates,1,span);Value orbit=pentOrbit?numericResult(states,samples,1,5,span):stateValues;
            std::map<std::string,Value> result{{"x",mapsPoint(arguments[1],input,last,span)},{"states",std::move(stateValues)},{"orbit",std::move(orbit)},{"n",numericResult(indices,samples,1,1,span)},
                {"jacobian",numericResult(finalJacobian,finalJacobian.empty()?0:input.variableCoordinates,finalJacobian.empty()?0:input.variableCoordinates,1,span)},
                {"iterations",I(iterations)},{"evaluations",I(evaluations)},{"jacobian_evaluations",I(jacobianEvaluations)},{"native_evaluations",I(bridge.nativeEvaluations)},{"samples",I(samples)},{"escaped",I(escaped)},
                {"escape_iteration",I(escapeIterations.empty()?0:static_cast<uint64_t>(escapeIterations.back()))},{"status",std::string(escaped?"escaped":"completed")},{"message",std::string(escaped?"At least one orbit exceeded its finite escape radius; no global classification is implied.":"Native iteration completed; finite samples do not prove boundedness, periodicity or chaos.")},
                {"cache_hit",I(native.cacheHit())},{"cache_key",native.key()},{"jacobian_method",jacobian},{"requested_threads",I(requestedThreads)},{"threads_used",I(1)}};
            if(diagram){result.emplace("parameter",numericResult(parameterRows,samples,1,1,span));result.emplace("scan_index",numericResult(scanIndices,samples,1,1,span));result.emplace("scan_values",numericResult(parameters,parameters.size(),1,1,span));result.emplace("final_states",numericResult(finalStates,parameters.size(),input.variableCoordinates,1,span));result.emplace("escaped_flags",numericResult(escapeFlags,parameters.size(),1,1,span));result.emplace("escape_iterations",numericResult(escapeIterations,parameters.size(),1,1,span));result.emplace("parameter_name",parameterName);}
            return makeRecord(std::move(result),span);
        }catch(const maps::Error& error){fail(error.status==PENTA_MAPS_CANCELLED?"R1006":"R2010",error.what(),span);}
        catch(const ode::OdeError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R2010",std::string("In native map: ")+error.what(),span);}
    }
