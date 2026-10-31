#include "dependency_pattern_helpers.inc"
    native_functions::Options derivativeNativeOptions(SourceSpan span) {
        if(options_.odeCacheDirectory.empty()&&options_.dataDirectory.empty())fail("N2002","Native numerical callbacks require a project/source cache directory.",span);
        native_functions::Options native;
        native.build.cacheDirectory=options_.odeCacheDirectory.empty()?options_.dataDirectory/".penta-cache"/"callbacks":options_.odeCacheDirectory.parent_path()/"callbacks";
        native.build.cancellation=options_.cancellation;
        native.build.cacheStatus=nativeCacheStatus();
        native.build.compilationFinished=[this](double seconds){recordCompilation(seconds);};
        native.chargeOperations=[this,span](std::size_t count){chargeNumericWork(count,span);};
        return native;
    }
    struct DerivativeInput {
        std::shared_ptr<const language::FunctionDefinition> definition;
        std::vector<double> flat;
        std::size_t variableCoordinates=0;
    };
    language::OdeVariable nativeParameter(const language::FunctionDefinition& function,std::size_t n,SourceSpan span) {
        if(n>=function.parameters.size())fail("N2001","Native callback parameter layout is incomplete.",span);
        auto parameter=function.parameters[n];
        if(n<function.parameterSchemas.size())parameter.schema=function.parameterSchemas[n];
        if(!language::coordinateWidth(parameter.type,parameter.schema))fail("N2001","Native callback parameter has an unsupported or unresolved scalar type.",span);
        return parameter;
    }
    std::vector<language::OdeVariable> nativeUnknownLayout(const language::FunctionDefinition& function,std::size_t coordinates,SourceSpan span) {
        std::vector<language::OdeVariable> result;std::size_t offset=0;
        for(std::size_t n=0;offset<coordinates&&n<function.parameters.size();++n){auto parameter=nativeParameter(function,n,span);offset+=language::coordinateWidth(parameter.type,parameter.schema);result.push_back(std::move(parameter));}
        if(offset!=coordinates)fail("N2001","A native unknown point must contain complete declared parameter coordinates.",span);
        return result;
    }
    std::vector<double> nativeDirectionCoordinates(const DerivativeInput& input,const std::vector<std::size_t>& active,const Value& value,SourceSpan span) {
        const auto* matrix=std::get_if<MatrixValue>(&value);const bool typed=std::holds_alternative<TensorValue>(value)||std::holds_alternative<UniversalMatrix>(value)||(matrix&&((*matrix)->pentarnion||((*matrix)->rows>1&&(*matrix)->columns>1)));
        if(!typed)return numericVector(value,span,false);
        std::size_t offset=0;for(std::size_t n=0;n<input.definition->parameters.size();++n){const auto parameter=nativeParameter(*input.definition,n,span);const auto width=language::coordinateWidth(parameter.type,parameter.schema);
            bool selected=active.size()==width;for(std::size_t k=0;selected&&k<width;++k)selected=active[k]==offset+k;
            if(selected&&(parameter.type==Type::RealMatrix||parameter.type==Type::PentarnionMatrix||parameter.type==Type::ElementMatrix||parameter.type==Type::Tensor)){std::vector<double> coordinates;appendNativeCoordinates(value,parameter,coordinates,span);return coordinates;}offset+=width;
        }
        fail("N2001","A typed array direction must match one complete active fixed-array parameter; use explicit flat real coordinates for combined parameters.",span);
    }
    std::vector<std::string> nativeUnknownCoordinateTags(const language::FunctionDefinition& function,std::size_t coordinates,SourceSpan span){
        std::vector<std::string> tags;
        for(const auto& parameter:nativeUnknownLayout(function,coordinates,span)){
            const auto width=language::coordinateWidth(parameter.type,parameter.schema);std::string identity;
            if(parameter.type==Type::Tensor){identity="tensor:"+std::string(language::typeName(parameter.schema->tensorEntryType))+":";for(const auto extent:parameter.schema->shape)identity+=std::to_string(extent)+"x";if(parameter.schema->algebra)identity+=":"+parameter.schema->algebra->hash();}
            else if(parameter.type==Type::Element||parameter.type==Type::ElementMatrix||parameter.type==Type::Complex)identity=native_functions::elementDefinition(parameter.type,parameter.schema)->hash();
            for(std::size_t c=0;c<width;++c)tags.push_back(identity.empty()?std::string():identity+":"+std::to_string(c));
        }
        return tags;
    }
    void requireNativeMapLayout(const language::FunctionDefinition& function,std::size_t coordinates,SourceSpan span) {
        const auto unknowns=nativeUnknownLayout(function,coordinates,span);
        const auto outputs=function.result==Type::Tuple?function.results:std::vector<Type>{function.result};
        const auto schemas=native_functions::callbackResultSchemas(function);
        const auto generic=[](Type type){return type==Type::Tensor||type==Type::Element||type==Type::Complex||type==Type::RealMatrix||type==Type::PentarnionMatrix||type==Type::ElementMatrix;};
        bool typed=false;for(const auto& input:unknowns)typed=typed||generic(input.type);for(const auto output:outputs)typed=typed||generic(output);
        if(!typed)return;
        if(outputs.size()!=unknowns.size())fail("N2401","A generic map must return the same declared state types and algebra definitions, in state order.",span);
        for(std::size_t n=0;n<outputs.size();++n){
            if(outputs[n]==Type::Tensor&&(!schemas[n]||!unknowns[n].schema||schemas[n]->shape!=unknowns[n].schema->shape||schemas[n]->tensorEntryType!=unknowns[n].schema->tensorEntryType||bool(schemas[n]->algebra)!=bool(unknowns[n].schema->algebra)||(schemas[n]->algebra&&schemas[n]->algebra->hash()!=unknowns[n].schema->algebra->hash())))fail("N2401","A native tensor map must preserve rank, shape and immutable entry kind/algebra.",span);
            if(outputs[n]!=unknowns[n].type)fail("N2401","A generic map cannot change its declared state type.",span);
            if((outputs[n]==Type::RealMatrix||outputs[n]==Type::PentarnionMatrix||outputs[n]==Type::ElementMatrix)&&(!schemas[n]||!unknowns[n].schema||schemas[n]->shape!=unknowns[n].schema->shape))fail("N2401","A native array map must preserve its declared state shape.",span);
            if((outputs[n]==Type::Element||outputs[n]==Type::ElementMatrix)&&(!schemas[n]||!schemas[n]->algebra||schemas[n]->algebra->hash()!=unknowns[n].schema->algebra->hash()))fail("N2401","A generic map cannot change its immutable algebra definition.",span);
        }
    }
    DerivativeInput derivativeInput(const Value& callable,const Value& point,const std::map<std::string,Value>* fields,SourceSpan span) {
        const auto* function=std::get_if<FunctionValue>(&callable);
        if(!function||!function->definition)fail("N2001","Differentiation requires a typed function handle.",span);
        auto definition=function->definition;
        if(!function->builtin.empty()){
            auto synthetic=std::make_shared<language::FunctionDefinition>(*definition);
            auto expression=std::make_shared<Expr>();expression->kind=ExprKind::Call;expression->text=function->builtin;expression->type=definition->result;expression->schema=definition->resultSchema;expression->span=span;
            for(std::size_t n=0;n<definition->parameters.size();++n){const auto& parameter=definition->parameters[n];auto value=std::make_shared<Expr>();value->kind=ExprKind::Reference;value->text=parameter.name;value->type=parameter.type;value->schema=n<definition->parameterSchemas.size()?definition->parameterSchemas[n]:parameter.schema;value->span=span;expression->children.push_back(std::move(value));}
            language::Statement returned;returned.kind=language::StatementKind::Return;returned.expression=std::move(expression);returned.span=span;synthetic->body={returned};definition=std::move(synthetic);
        }
        DerivativeInput result;result.definition=definition;std::size_t supplied=0;
        auto schema=[&](std::size_t n){return n<definition->parameterSchemas.size()?definition->parameterSchemas[n]:definition->parameters[n].schema;};
        auto width=[&](std::size_t n){const auto count=language::coordinateWidth(definition->parameters[n].type,schema(n));if(!count)fail("N2001","Compiled callbacks require real, complex, Pentarnion or typed algebra scalar parameters; matrix/series parameters are unsupported.",span);
        return count;};
        auto append=[&](const Value& value,std::size_t n){auto parameter=definition->parameters[n];parameter.schema=schema(n);appendNativeCoordinates(value,parameter,result.flat,span);};
        if(const auto* tuple=std::get_if<TupleValue>(&point)){
            if((*tuple)->values.size()>definition->parameters.size())fail("N2001","Too many callback point values.",span);
            for(const auto& value:(*tuple)->values){append(value,supplied);++supplied;}
        }else if(std::holds_alternative<UniversalMatrix>(point)||(!definition->parameters.empty()&&(definition->parameters.front().type==Type::RealMatrix||definition->parameters.front().type==Type::PentarnionMatrix||definition->parameters.front().type==Type::ElementMatrix))){
            if(definition->parameters.empty())fail("N2001","A native array point requires a declared fixed-array parameter.",span);
        append(point,0);supplied=1;
        }else if(std::holds_alternative<MatrixValue>(point)||rangeValue(point)){
            const auto values=numericVector(point,span,false);result.flat=values;
            std::size_t coordinates=0;while(supplied<definition->parameters.size()&&coordinates<values.size())coordinates+=width(supplied++);
            if(coordinates!=values.size())fail("N2001","A flat real point must contain complete declared parameter coordinates.",span);
        }else{
            if(definition->parameters.empty())fail("N2001","A differentiated callback requires a numeric input.",span);
            append(point,0);supplied=1;
        }
        result.variableCoordinates=result.flat.size();
        const std::map<std::string,Value>* fixed=nullptr;
        if(fields)if(const auto found=fields->find("parameters");found!=fields->end()){fixed=recordFields(found->second);if(!fixed)fail("N2001","parameters must be an options record of trailing fixed callback arguments.",span);}
        std::set<std::string> used;
        for(std::size_t n=supplied;n<definition->parameters.size();++n){const auto& parameter=definition->parameters[n];
            if(fixed){const auto found=fixed->find(parameter.name);if(found!=fixed->end()){append(found->second,n);used.insert(parameter.name);continue;}}
            if(n<definition->parameterDefaults.size()&&definition->parameterDefaults[n]){append(evaluate(*definition->parameterDefaults[n]),n);continue;}
            fail("N2001","Missing fixed native callback parameter: "+parameter.name,span);
        }
        if(fixed&&used.size()!=fixed->size())fail("N2001","Unknown, repeated or nontrailing fixed native callback parameter.",span);
        return result;
    }
    std::optional<Value> derivativeCall(const Expr& expression,const std::vector<Value>& arguments) {
        if(auto pattern=dependencyPatternCall(expression,arguments))return pattern;
        const auto& name=expression.text;const auto span=expression.span;
        const bool product=name=="jvp"||name=="vjp"||name=="hvp";
        const bool supported=name=="jacobian"||name=="hessian"||name=="derivative_check"||product||name=="gradient";
        if(!supported||(name=="gradient"&&(arguments.empty()||!std::holds_alternative<FunctionValue>(arguments[0]))))return {};
        const std::size_t mandatory=product?3:2;
        if(arguments.size()<mandatory||arguments.size()>mandatory+1)fail("N2001","Use derivative(function,point[,options]) or a product(function,point,direction[,options]).",span);
        const auto* fields=arguments.size()>mandatory?recordFields(arguments.back()):nullptr;
        if(arguments.size()>mandatory&&!fields)fail("N2001","Derivative options must be a typed record.",span);
        const std::set<std::string> allowed{"method","step","wrt","parameters","threads","order","tolerance"};
        if(fields)for(const auto& [key,value]:*fields){(void)value;if(!allowed.contains(key))fail("N2001","Unknown derivative option: "+key,span);}
        const auto input=derivativeInput(arguments[0],arguments[1],fields,span);
        native_functions::DerivativeRequest request;request.order=name=="hessian"||name=="hvp"?2:1;request.requestedThreads=parallel_.settings.threads;
        request.active.resize(input.variableCoordinates);std::iota(request.active.begin(),request.active.end(),0);
        if(fields){
            if(name!="derivative_check"&&(fields->contains("order")||fields->contains("tolerance")))fail("N2001","order and tolerance options apply only to derivative_check.",span);
            if(const auto found=fields->find("method");found!=fields->end()){const auto* method=std::get_if<std::string>(&found->second);if(!method)fail("N2001","Derivative method must be analytic, ad, automatic or finite_difference.",span);
        if(*method=="finite_difference")request.method=native_functions::DerivativeMethod::FiniteDifference;else if(*method!="analytic"&&*method!="ad"&&*method!="automatic")fail("N2001","Unknown derivative method.",span);}
            if(const auto found=fields->find("step");found!=fields->end()){request.step=real(found->second,span);if(request.step<0)fail("N2001","Derivative step must be nonnegative (0 selects automatic).",span);}
            if(const auto found=fields->find("threads");found!=fields->end()){const auto requested=integer(found->second,span);if(requested<0||requested>64)fail("N2001","threads must be an Integer in0..64.",span);request.requestedThreads=static_cast<std::size_t>(requested);}
            if(const auto found=fields->find("wrt");found!=fields->end()){
                request.active.clear();
                if(const auto* parameter=std::get_if<std::string>(&found->second)){std::size_t offset=0;bool selected=false;for(std::size_t k=0;k<input.definition->parameters.size();++k){const auto& p=input.definition->parameters[k];const auto schema=k<input.definition->parameterSchemas.size()?input.definition->parameterSchemas[k]:p.schema;const auto count=language::coordinateWidth(p.type,schema);if(!count)fail("N2001","Unsupported native wrt parameter shape.",span);
        if(p.name==*parameter){for(std::size_t c=0;c<count;++c)request.active.push_back(offset+c);selected=true;}offset+=count;}if(!selected)fail("N2001","Unknown wrt callback parameter.",span);}
                else{const auto selected=numericVector(found->second,span,false);for(const auto coordinate:selected){if(coordinate<1||coordinate>input.flat.size()||coordinate!=std::trunc(coordinate))fail("N2001","wrt indices must be one-based real input coordinate indices.",span);request.active.push_back(static_cast<std::size_t>(coordinate-1));}}
            }
        }
        try{
            native_functions::Callback callback(*input.definition,derivativeNativeOptions(span));
            if((name=="gradient"||name=="hessian"||name=="hvp")&&callback.outputCount()!=1)fail("N2001","Gradient/Hessian/HVP require one real scalar objective; use jacobian for a vector map.",span);
            if(product&&name!="vjp")request.direction=nativeDirectionCoordinates(input,request.active,arguments[2],span);
            if(name=="derivative_check"){
                if(fields)if(const auto found=fields->find("order");found!=fields->end()){const auto order=integer(found->second,span);if(order<1||order>2)fail("N2001","Derivative check order must be1 or2.",span);request.order=static_cast<unsigned>(order);}
                request.method=native_functions::DerivativeMethod::Automatic;const auto analytic=callback.evaluate(context_.gamma(),input.flat,request);
                request.method=native_functions::DerivativeMethod::FiniteDifference;const auto numerical=callback.evaluate(context_.gamma(),input.flat,request);
                double maxError=0,scale=0;const auto& a=request.order==2?analytic.second:analytic.jacobian;const auto& b=request.order==2?numerical.second:numerical.jacobian;
                for(std::size_t n=0;n<a.size();++n){maxError=std::max(maxError,std::abs(a[n]-b[n]));scale=std::max(scale,std::max(std::abs(a[n]),std::abs(b[n])));}
                double tolerance=request.order==2?2e-3:1e-5;if(fields)if(const auto found=fields->find("tolerance");found!=fields->end())tolerance=real(found->second,span);if(tolerance<=0)fail("N2001","Derivative verification tolerance must be positive.",span);
                const double relative=maxError/(1+scale);return makeRecord({{"max_abs_error",maxError},{"relative_error",relative},{"passed",I(relative<=tolerance)},{"tolerance",tolerance},{"order",I(request.order)},{"native_evaluations",I(analytic.nativeEvaluations+numerical.nativeEvaluations)},{"cache_hit",I(analytic.cacheHit)},{"cache_key",analytic.key},{"threads_requested",I(request.requestedThreads)},{"threads_used",I(1)}},span);
            }
            const auto result=callback.evaluate(context_.gamma(),input.flat,request);const auto n=request.active.size();
            if(name=="hessian")return numericResult(result.second,n,n,1,span);
            if(name=="hvp")return numericResult(result.second,n,1,1,span);
            if(name=="gradient")return numericResult(result.jacobian,n,1,1,span);
            if(name=="vjp"){std::vector<double> weights;if(std::holds_alternative<TensorValue>(arguments[2])){const auto schemas=native_functions::callbackResultSchemas(*input.definition);if(callback.outputTypes().size()!=1||callback.outputTypes()[0]!=Type::Tensor)fail("N2001","A tensor VJP weight requires one complete tensor output; use flat real weights for combined results.",span);appendNativeCoordinates(arguments[2],language::OdeVariable{"weights",Type::Tensor,span,schemas[0]},weights,span);}else weights=numericVector(arguments[2],span,false);if(weights.size()!=callback.outputCount())fail("N2001","VJP weights must match the real output coordinate count.",span);std::vector<double> output(n,0);
        for(std::size_t o=0;o<weights.size();++o)for(std::size_t j=0;j<n;++j)output[j]+=weights[o]*result.jacobian[o*n+j];
        return numericResult(output,n,1,1,span);}
            return numericResult(result.jacobian,callback.outputCount(),result.columns,1,span);
        }catch(const ode::OdeError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":error.code,std::string("In compiled derivative callback: ")+error.what(),span);}
    }
