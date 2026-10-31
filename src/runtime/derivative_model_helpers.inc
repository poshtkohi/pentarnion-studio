    std::optional<Value> derivativeModelCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        const bool product=name=="jvp"||name=="vjp"||name=="hvp";
        if(name!="jacobian"&&name!="gradient"&&name!="hessian"&&name!="derivative_check"&&!product)return {};
        if(arguments.empty()||!std::holds_alternative<ModelValue>(arguments[0]))return {};
        const std::size_t mandatory=product?3:2;
        if(arguments.size()<mandatory||arguments.size()>mandatory+1)fail("N2010","Use derivative(model,state[,options]) or product(model,state,direction[,options]).",span);
        const auto& model=std::get<ModelValue>(arguments[0]);if(!model.definition||model.definition->states.empty())fail("N2010","A typed ODE model definition is required.",span);
        const auto& source=*model.definition;
        const auto* fields=arguments.size()>mandatory?recordFields(arguments.back()):nullptr;
        if(arguments.size()>mandatory&&!fields)fail("N2010","ODE derivative options require a typed record.",span);
        std::map<std::string,Value> forwardedFields=fields?*fields:std::map<std::string,Value>{};
        double time=0;
        if(const auto found=forwardedFields.find("time");found!=forwardedFields.end()){time=real(found->second,span);forwardedFields.erase(found);}
        std::map<std::string,Value> parameters;
        if(const auto found=forwardedFields.find("parameters");found!=forwardedFields.end()){const auto* supplied=recordFields(found->second);if(!supplied)fail("N2010","ODE parameters must be a typed options record.",span);parameters=*supplied;}
        std::set<std::string> parameterNames;for(const auto& parameter:source.parameters)parameterNames.insert(parameter.name);
        for(const auto& [key,value]:parameters){(void)value;if(!parameterNames.contains(key))fail("N2010","Unknown ODE parameter (state/time belong outside parameters): "+key,span);}
        for(const auto& parameter:source.parameters)if(!parameters.contains(parameter.name))fail("N2010","Missing ODE derivative parameter: "+parameter.name,span);
        parameters.emplace(source.timeName,time);
        forwardedFields.insert_or_assign("parameters",makeRecord(std::move(parameters),span));
        const auto& point=arguments[1];std::size_t stateCoordinates=0;for(const auto& state:source.states)stateCoordinates+=language::coordinateWidth(state.type,state.schema);
        if(const auto* tuple=std::get_if<TupleValue>(&point)){if((*tuple)->values.size()!=source.states.size())fail("N2010","The ODE derivative state tuple must supply every declared state once.",span);}
        else if(std::holds_alternative<MatrixValue>(point)||rangeValue(point)){const auto packed=numericVector(point,span,false);if(packed.size()!=stateCoordinates)fail("N2010","The ODE derivative point must contain exactly the declared state coordinates, without parameters/time.",span);}
        else if(source.states.size()!=1)fail("N2010","A multi-state ODE derivative needs a typed tuple or flat real state vector.",span);
        auto definition=std::make_shared<language::FunctionDefinition>(ode::rhsFunction(source));
        auto forwarded=arguments;forwarded[0]=FunctionValue{std::move(definition),{},{}};
        if(forwarded.size()>mandatory)forwarded.back()=makeRecord(std::move(forwardedFields),span);else forwarded.push_back(makeRecord(std::move(forwardedFields),span));
        return derivativeCall(expression,forwarded);
    }
