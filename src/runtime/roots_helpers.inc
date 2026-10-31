    std::optional<Value> rootsCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(name!="root"&&name!="rootsolve"&&name!="equilibrium"&&name!="fixedpoint")return {};
        requireOutsideIterationObserver(span);
        if(arguments.size()<2||arguments.size()>3)fail("R1980","Use root(function,bracket[,options]) or rootsolve/equilibrium/fixedpoint(callback,initial[,options]).",span);
        const auto* fields=arguments.size()==3?recordFields(arguments[2]):nullptr;
        if(arguments.size()==3&&!fields)fail("R1980","Root options must be a typed record.",span);
        const std::set<std::string> allowed{"parameters","method","jacobian","xtol","rtol","ftol","max_iterations","max_evaluations","max_wall_seconds","max_newton_step","variable_scale","residual_scale","threads","OutputFcn","OutputEvery"};
        if(fields)for(const auto& [key,value]:*fields){(void)value;if(!allowed.contains(key))fail("R1980","Unknown root option: "+key,span);}
        auto option=[&](const char* key)->const Value*{if(!fields)return nullptr;const auto found=fields->find(key);return found==fields->end()?nullptr:&found->second;};
        auto number=[&](const char* key,double fallback){const auto* value=option(key);return value?real(*value,span):fallback;};
        auto count=[&](const char* key,I fallback){const auto* value=option(key);const auto valueCount=value?integer(*value,span):fallback;if(valueCount<1)fail("R1980",std::string(key)+" must be a positive Integer.",span);return static_cast<uint64_t>(valueCount);};
        auto text=[&](const char* key,const std::string& fallback){const auto* value=option(key);if(!value)return fallback;const auto* result=std::get_if<std::string>(value);if(!result)fail("R1980",std::string(key)+" must be a string.",span);return *result;};
        const bool scalarRoot=name=="root";
        const I requestedThreads=option("threads")?integer(*option("threads"),span):static_cast<I>(parallel_.settings.threads);
        if(requestedThreads<0||requestedThreads>64)fail("R1980","Root threads must be an Integer in 0..64; current native scalar/dense KINSOL evaluation is serial.",span);
        const std::string method=text("method",scalarRoot?"toms748":"line_search"),jacobianMethod=text("jacobian","analytic");
        if(scalarRoot&&method!="toms748")fail("R1980","Scalar root currently uses method=\"toms748\".",span);
        if(!scalarRoot&&method!="newton"&&method!="line_search")fail("R1980","System root method must be newton or line_search.",span);
        if(jacobianMethod!="analytic"&&jacobianMethod!="ad"&&jacobianMethod!="automatic"&&jacobianMethod!="finite_difference")fail("R1980","Root Jacobian must be analytic/ad/automatic or finite_difference.",span);
        if(!scalarRoot&&option("rtol"))fail("R1980","rtol applies to scalar brackets; KINSOL uses xtol as its scaled-step tolerance.",span);
        if(scalarRoot&&option("variable_scale"))fail("R1980","variable_scale applies to KINSOL systems; scale the scalar bracket explicitly.",span);
        Value callable=arguments[0];
        if(name=="equilibrium"){
            const auto* model=std::get_if<ModelValue>(&callable);if(!model||!model->definition)fail("R1980","equilibrium requires a typed autonomous ODE model.",span);
            const auto& odeDefinition=*model->definition;
            std::function<void(const language::ExprPtr&)> autonomous=[&](const language::ExprPtr& value){if(!value)return;if(value->kind==ExprKind::Reference&&value->text==odeDefinition.timeName)fail("R1980","equilibrium requires an autonomous ODE: its RHS may not reference time.",span);for(const auto& child:value->children)autonomous(child);};
            for(const auto& value:odeDefinition.derivatives)autonomous(value);
            auto definition=std::make_shared<language::FunctionDefinition>();definition->name="equilibrium_"+odeDefinition.name;definition->native=true;definition->span=span;definition->parameters=odeDefinition.states;definition->parameters.insert(definition->parameters.end(),odeDefinition.parameters.begin(),odeDefinition.parameters.end());
            for(const auto& parameter:definition->parameters)definition->parameterSchemas.push_back(parameter.schema);
            language::Statement returned;returned.kind=language::StatementKind::Return;returned.span=span;
            if(odeDefinition.states.size()==1){definition->result=odeDefinition.states[0].type;definition->resultSchema=odeDefinition.states[0].schema;returned.expression=odeDefinition.derivatives[0];}
            else{definition->result=Type::Tuple;auto schema=std::make_shared<language::ValueSchema>();for(const auto& state:odeDefinition.states){definition->results.push_back(state.type);schema->elements.push_back({{},state.type,state.schema});}definition->resultSchema=schema;auto tuple=std::make_shared<Expr>();tuple->kind=ExprKind::TupleLiteral;tuple->type=Type::Tuple;tuple->schema=std::move(schema);tuple->span=span;tuple->children=odeDefinition.derivatives;returned.expression=std::move(tuple);}
            definition->body={std::move(returned)};callable=FunctionValue{std::move(definition),{},{}};
        }
        std::vector<double> bracket;
        Value point=arguments[1];
        if(scalarRoot){bracket=numericVector(arguments[1],span,false);if(bracket.size()!=2)fail("R1980","root bracket must have exactly two finite real entries.",span);point=bracket[0];}
        const auto input=derivativeInput(callable,point,fields,span);
        if(name=="fixedpoint")requireNativeMapLayout(*input.definition,input.variableCoordinates,span);
        if(input.variableCoordinates<1||input.variableCoordinates>128)fail("R1980","Root unknowns require 1..128 real coordinates.",span);
        if(scalarRoot&&(input.variableCoordinates!=1||input.definition->parameters.front().type!=Type::Real))fail("R1980","Scalar root requires its first unknown argument to be real.",span);
        penta_roots_request_v1 request{};request.struct_size=sizeof(request);request.operation=scalarRoot?PENTA_ROOTS_SCALAR:name=="fixedpoint"?PENTA_ROOTS_FIXEDPOINT:PENTA_ROOTS_SYSTEM;request.dimension=input.variableCoordinates;
        request.max_iterations=count("max_iterations",200);request.max_evaluations=count("max_evaluations",10000);request.line_search=method=="line_search";request.finite_difference=jacobianMethod=="finite_difference";
        request.absolute_tolerance=number("xtol",1e-12);request.relative_tolerance=number("rtol",1e-12);request.residual_tolerance=number("ftol",1e-10);request.maximum_wall_seconds=number("max_wall_seconds",60);
        request.maximum_newton_step=number("max_newton_step",0);
        if(scalarRoot){request.lower=bracket[0];request.upper=bracket[1];}request.initial=input.flat.data();
        std::vector<double> variableScale,residualScale;
        if(const auto* value=option("variable_scale")){variableScale=numericVector(*value,span,false);if(variableScale.size()!=request.dimension)fail("R1980","variable_scale length must equal the real coordinate dimension.",span);request.variable_scale=variableScale.data();}
        if(const auto* value=option("residual_scale")){residualScale=numericVector(*value,span,false);if(residualScale.size()!=request.dimension)fail("R1980","residual_scale length must equal the real coordinate dimension.",span);request.residual_scale=residualScale.data();}
        try{
            native_functions::Callback native(*input.definition,derivativeNativeOptions(span));
            if(native.outputCount()!=request.dimension||native.inputCount()!=input.flat.size())fail("R1980","A root system must have exactly as many real residual coordinates as unknown coordinates.",span);
            struct Bridge {
                const native_functions::Callback* native;double gamma;std::vector<double> input;native_functions::DerivativeRequest derivative;uint64_t nativeEvaluations=0;std::exception_ptr failure;
                static void message(char* error,uint64_t capacity,const char* text)noexcept{if(!error||!capacity)return;uint64_t at=0;while(at+1<capacity&&text[at]){error[at]=text[at];++at;}error[at]=0;}
                static int evaluate(void* raw,const double* x,uint64_t count,double* values,double* jacobian,char* error,uint64_t capacity)noexcept{
                    auto& bridge=*static_cast<Bridge*>(raw);try{std::copy_n(x,static_cast<std::size_t>(count),bridge.input.begin());bridge.derivative.order=jacobian?1u:0u;const auto result=bridge.native->evaluate(bridge.gamma,bridge.input,bridge.derivative);
                        bridge.nativeEvaluations+=result.nativeEvaluations;
                        if(result.values.size()!=count||(jacobian&&result.jacobian.size()!=count*count))throw std::runtime_error("Native callback returned an invalid root layout.");
                        std::copy(result.values.begin(),result.values.end(),values);if(jacobian)std::copy(result.jacobian.begin(),result.jacobian.end(),jacobian);return 0;
                    }catch(const std::exception& failure){bridge.failure=std::current_exception();message(error,capacity,failure.what());return PENTA_ROOTS_CALLBACK;}catch(...){bridge.failure=std::current_exception();message(error,capacity,"Native root callback failed.");return PENTA_ROOTS_CALLBACK;}
                }
            } bridge{&native,context_.gamma(),input.flat,{},0,{}};
            bridge.derivative.active.resize(input.variableCoordinates);std::iota(bridge.derivative.active.begin(),bridge.derivative.active.end(),0);bridge.derivative.requestedThreads=static_cast<std::size_t>(requestedThreads);
            request.evaluate=Bridge::evaluate;request.callback_context=&bridge;
            roots::Result solved;
            try{solved=roots::Module(options_.numericModuleDirectory).solve(request,options_.cancellation,[this,span](std::size_t work){chargeNumericWork(work,span);},makeIterationObserver(fields,span));}
            catch(...){if(bridge.failure)std::rethrow_exception(bridge.failure);throw;}
            Value x;
            if(scalarRoot)x=solved.solution[0];
            else x=solverPointResult(solved.solution,point,*input.definition,span);
            const auto& report=solved.report;
            Value finalBracket=scalarRoot?numericResult({report.lower,report.upper},1,2,1,span):numericResult({},0,0,1,span);
            return makeRecord({{"x",std::move(x)},{"residuals",numericResult(solved.residual,request.dimension,1,1,span)},
                {"residual_norm",report.residual_norm},{"scaled_residual_norm",report.scaled_residual_norm},{"converged",I(report.converged)},{"status",std::string(report.status)},{"message",std::string(report.message)},
                {"iterations",I(report.iterations)},{"evaluations",I(report.evaluations)},{"jacobian_evaluations",I(report.jacobian_evaluations)},{"backend_evaluations",I(report.backend_evaluations)},{"native_evaluations",I(bridge.nativeEvaluations)},
                {"step_norm",report.step_norm},{"flag",I(report.backend_flag)},{"bracket",std::move(finalBracket)},{"method",method},{"jacobian",scalarRoot?std::string("not_used"):jacobianMethod},
                {"cache_hit",I(native.cacheHit())},{"cache_key",native.key()},{"requested_threads",requestedThreads},{"threads_used",I(1)}},span);
        }catch(const roots::Error& error){fail(error.status==PENTA_ROOTS_CANCELLED?"R1006":"R1980",error.what(),span);}
        catch(const ode::OdeError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R1980",error.what(),span);}
    }
