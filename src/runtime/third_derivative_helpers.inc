    std::optional<Value> thirdDerivativeCall(const Expr& expression,const std::vector<Value>& arguments){
        if(expression.text!="thirdderivative")return {};
        const auto span=expression.span;
        if(arguments.size()<5||arguments.size()>6)fail("N2301","Use thirdderivative(function,point,u,v,w[,options(parameters=...)]).",span);
        const auto* fields=arguments.size()==6?recordFields(arguments[5]):nullptr;
        if(arguments.size()==6&&!fields)fail("N2301","Third-derivative options require a typed record.",span);
        if(fields)for(const auto& [key,value]:*fields){(void)value;if(key!="parameters")fail("N2301","Unknown third-derivative option: "+key+". All three directions are analytic; there is no finite-difference mode.",span);}
        const auto input=derivativeInput(arguments[0],arguments[1],fields,span);
        if(input.variableCoordinates==0)fail("N2301","Third derivatives require at least one active real coordinate.",span);
        auto direction=[&](const Value& value){
            if(std::holds_alternative<TensorValue>(value)||std::holds_alternative<UniversalMatrix>(value)||(std::holds_alternative<MatrixValue>(value)&&(std::get<MatrixValue>(value)->pentarnion||(std::get<MatrixValue>(value)->rows>1&&std::get<MatrixValue>(value)->columns>1)))){std::vector<std::size_t> active(input.variableCoordinates);std::iota(active.begin(),active.end(),0);return nativeDirectionCoordinates(input,active,value,span);}
            if(const auto* p=std::get_if<P>(&value)){
                if(input.variableCoordinates!=5||input.definition->parameters.empty()||input.definition->parameters.front().type!=Type::Pentarnion)fail("N2301","A Pentarnion direction requires a single Pentarnion active point; otherwise supply flattened real coordinates.",span);
                return std::vector<double>(p->coefficients().begin(),p->coefficients().end());
            }
            if(const auto* element=std::get_if<UniversalElement>(&value)){
                if(input.definition->parameters.empty()||input.definition->parameters.front().type!=Type::Element)fail("N2301","An algebra direction requires one active point from the same immutable algebra.",span);
                const auto& parameter=input.definition->parameters.front();const auto schema=input.definition->parameterSchemas.empty()?parameter.schema:input.definition->parameterSchemas.front();
                if(!schema||!schema->algebra||schema->algebra->hash()!=element->definition()->hash()||input.variableCoordinates!=element->coefficients().size())fail("N2301","Algebra direction must match the full active point dimension and immutable definition.",span);
                return element->coefficients();
            }
            if(const auto* z=std::get_if<std::complex<double>>(&value)){
                if(input.variableCoordinates!=2||input.definition->parameters.empty()||input.definition->parameters.front().type!=Type::Complex)fail("N2301","A complex direction requires one active complex point; otherwise supply flattened real coordinates.",span);
                return std::vector<double>{z->real(),z->imag()};
            }
            auto values=numericVector(value,span,false);
            if(values.size()==1&&input.variableCoordinates==5&&input.definition->parameters.front().type==Type::Pentarnion)values.resize(5,0);
            if(values.size()!=input.variableCoordinates)fail("N2301","Each third-derivative direction must contain every active real coordinate in declaration and algebra-basis order.",span);
            return values;
        };
        native_functions::ThirdRequest request;request.active.resize(input.variableCoordinates);std::iota(request.active.begin(),request.active.end(),0);
        request.u=direction(arguments[2]);request.v=direction(arguments[3]);request.w=direction(arguments[4]);
        try{
            native_functions::ThirdCallback callback(*input.definition,derivativeNativeOptions(span));
            native_functions::ThirdResult result;
            ThreadBudget::instance().withReservation([&]{result=callback.evaluate(context_.gamma(),input.flat,request);});
            std::vector<double> widths,indices(input.variableCoordinates);for(const auto width:callback.outputWidths())widths.push_back(static_cast<double>(width));std::iota(indices.begin(),indices.end(),1.);
            std::string algebraDescription(context_.id());
            if(native_functions::usesUniversalCoordinates(*input.definition)){
                std::set<std::string> identities;
                auto add=[&](Type type,const std::shared_ptr<const language::ValueSchema>& schema){if(type==Type::Element||type==Type::Complex||type==Type::ElementMatrix){const auto definition=native_functions::elementDefinition(type,schema);identities.insert(definition->name()+":"+definition->hash());}else if((type==Type::RealMatrix||type==Type::PentarnionMatrix)&&schema&&schema->shape.size()==2)identities.insert(std::string(type==Type::RealMatrix?"real-array:":"Pentarnion-array:")+std::to_string(schema->shape[0])+"x"+std::to_string(schema->shape[1]));};
                for(std::size_t n=0;n<input.definition->parameters.size();++n)add(input.definition->parameters[n].type,n<input.definition->parameterSchemas.size()?input.definition->parameterSchemas[n]:input.definition->parameters[n].schema);
                const auto schemas=native_functions::callbackResultSchemas(*input.definition);for(std::size_t n=0;n<callback.outputTypes().size();++n)add(callback.outputTypes()[n],schemas[n]);
                algebraDescription.clear();for(const auto& identity:identities){if(!algebraDescription.empty())algebraDescription+=';';algebraDescription+=identity;}
            }
            ++parallel_.statistics.serialCalls;parallel_.statistics.threadsUsed=1;
            return makeRecord({{"values",numericResult(result.values,callback.outputCount(),1,1,span)},{"third",numericResult(result.third,callback.outputCount(),1,1,span)},
                {"u",numericResult(request.u,input.variableCoordinates,1,1,span)},{"v",numericResult(request.v,input.variableCoordinates,1,1,span)},{"w",numericResult(request.w,input.variableCoordinates,1,1,span)},
                {"active_coordinates",numericResult(indices,1,indices.size(),1,span)},{"output_widths",numericResult(widths,1,widths.size(),1,span)},
                {"input_coordinates",I(input.variableCoordinates)},{"total_input_coordinates",I(input.flat.size())},{"output_coordinates",I(callback.outputCount())},
                {"native_evaluations",I(result.nativeEvaluations)},{"threads_used",I(1)},{"cache_hit",I(result.cacheHit)},{"cache_key",result.key},
                {"method",std::string("analytic_hyperdual3")},{"algebra",std::move(algebraDescription)},{"gamma",context_.gamma()}},span);
        }catch(const ode::OdeError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":error.code,std::string("In compiled third derivative: ")+error.what(),span);}
    }
