    std::optional<Type> checkOptimizationCall(const ExprPtr& value){
        if(value->text!="minimize")return {};
        const auto count=value->children.size();bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2030",text,value->span);valid=false;}};
        if(count==3&&!checkIterationOptions(value->children.back()))valid=false;
        require(m_options.languageVersion=="0.2","Native optimization requires language0.2.");require(!hasNamedArguments(value),"Optimization uses positional arguments and a final typed options record.");require(count==2||count==3,"Use minimize(objective,initial[,options]).");if(count<2)return Type::Invalid;require(value->children[0]->type==Type::Function,"minimize requires a typed native-compilable objective.");if(count==3)require(value->children[2]->type==Type::Record,"Optimization options require a typed record.");
        const auto point=value->children[1];require(scalarType(point->type)||point->type==Type::Pentarnion||point->type==Type::Element||point->type==Type::ElementMatrix||point->type==Type::Tensor||point->type==Type::Complex||matrixType(point->type)||rangeType(point->type)||point->type==Type::Tuple,"Optimization initial point must be numeric coordinates or a typed tuple.");
        const auto function=value->children[0]->function?value->children[0]->function:value->children[0]->schema?value->children[0]->schema->function:nullptr;if(function){require(function->result==Type::Real,"minimize requires one real scalar objective.");for(std::size_t n=0;n<function->parameters.size();++n)require(nativeSolverScalar(function->parameters[n].type,nativeSolverParameterSchema(*function,n)),"Native objective inputs require numeric scalars or explicitly shaped arrays.");}
        if(function&&!checkNativeSolverPoint(point,*function,"E2030"))valid=false;
        if(point->type==Type::PentarnionMatrix)require(solverArrayPoint(function),"A whole Pentarnion matrix objective point requires an explicitly shaped matrix parameter.");
        std::shared_ptr<const ValueSchema> pointSchema;const Type pointType=solverPointType(point,function,pointSchema);auto schema=std::make_shared<ValueSchema>();schema->fields.push_back({"x",pointType,pointSchema});
        for(const auto name:{"objective","constraint_violation","elapsed_seconds"})schema->fields.push_back({name,Type::Real,{}});
        for(const auto name:{"feasible","converged","flag","evaluations","objective_evaluations","constraint_evaluations","gradient_evaluations","native_evaluations","seed","cache_hit","threads_requested","threads_used"})schema->fields.push_back({name,Type::Integer,{}});
        for(const auto name:{"status","message","algorithm","jacobian_method","backend","cache_key"})schema->fields.push_back({name,Type::String,{}});
        schema->fields.push_back({"history",Type::RealMatrix,{}});value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
