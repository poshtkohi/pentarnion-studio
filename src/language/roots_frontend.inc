    std::optional<Type> checkRootsCall(const ExprPtr& value) {
        const auto& name=value->text;if(name!="root"&&name!="rootsolve"&&name!="equilibrium"&&name!="fixedpoint")return {};
        bool valid=true;const auto count=value->children.size();
        if(count==3&&!checkIterationOptions(value->children.back()))valid=false;
        if(m_options.languageVersion!="0.2"){add("E1601","Native root finding requires language 0.2.",value->span);valid=false;}
        auto require=[&](bool okay,const char* text){if(!okay){add("E1980",text,value->span);valid=false;}};
        require(count==2||count==3,"Use root(function,bracket[,options]) or rootsolve/equilibrium/fixedpoint(callback,initial[,options]).");
        require(!hasNamedArguments(value),"Root builtins use positional arguments and a typed options record.");
        if(count<2)return Type::Invalid;
        const bool scalarRoot=name=="root",equilibrium=name=="equilibrium";
        require(value->children[0]->type==(equilibrium?Type::OdeModel:Type::Function),equilibrium?"equilibrium requires an autonomous ODE model.":"Root finding requires a typed function handle.");
        if(count==3)require(value->children[2]->type==Type::Record,"Root options require a typed record.");
        std::shared_ptr<const FunctionDefinition> function=value->children[0]->function;
        const auto ode=value->children[0]->ode;
        if(!function&&value->children[0]->schema)function=value->children[0]->schema->function;
        if(!equilibrium&&function){require(!function->parameters.empty(),"Root callbacks require at least one real, complex or typed algebra input.");for(std::size_t n=0;n<function->parameters.size();++n)require(nativeSolverScalar(function->parameters[n].type,nativeSolverParameterSchema(*function,n)),"Root callbacks require real/complex/algebra scalars or explicitly shaped native arrays.");
            if(scalarRoot){require(!function->parameters.empty()&&function->parameters.front().type==Type::Real,"Scalar root requires a real first argument.");require(function->result==Type::Real,"Scalar root requires one real returned residual.");}
            else{const auto types=function->result==Type::Tuple?function->results:std::vector<Type>{function->result};for(std::size_t n=0;n<types.size();++n){const auto shape=function->result==Type::Tuple?(function->resultSchema&&n<function->resultSchema->elements.size()?function->resultSchema->elements[n].schema:nullptr):function->resultSchema;require(nativeSolverScalar(types[n],shape),"Root residuals require numeric scalar elements or explicitly shaped native arrays.");}}
        }
        const auto point=value->children[1];Type xType=Type::Real;std::shared_ptr<const ValueSchema> xSchema;
        if(scalarRoot)require(matrixType(point->type)||rangeType(point->type),"Scalar root requires a two-element real bracket.");
        else{
            require(scalarType(point->type)||point->type==Type::Pentarnion||point->type==Type::Element||point->type==Type::ElementMatrix||point->type==Type::Tensor||point->type==Type::Complex||point->type==Type::Tuple||matrixType(point->type)||rangeType(point->type),"Root initial point requires real, complex or typed algebra scalars, a real vector or typed tuple.");
            
            auto pointDefinition=function;
            if(equilibrium&&ode){auto states=std::make_shared<FunctionDefinition>();states->parameters=ode->states;for(const auto& state:ode->states)states->parameterSchemas.push_back(state.schema);pointDefinition=std::move(states);}
            if(point->type==Type::PentarnionMatrix)require(solverArrayPoint(pointDefinition),"A whole Pentarnion matrix root point requires an explicitly shaped matrix parameter.");
            xType=solverPointType(point,pointDefinition,xSchema);
            if(pointDefinition&&!checkNativeSolverPoint(point,*pointDefinition,"E1980"))valid=false;
        }
        if(equilibrium&&ode){std::function<void(const ExprPtr&)> visit=[&](const ExprPtr& expression){if(!expression)return;if(expression->kind==ExprKind::Reference&&expression->text==ode->timeName){add("E1980","equilibrium requires an autonomous ODE; its RHS references time.",value->span);valid=false;}for(const auto& child:expression->children)visit(child);};for(const auto& expression:ode->derivatives)visit(expression);}
        auto schema=std::make_shared<ValueSchema>();schema->fields={{"x",xType,xSchema},{"residuals",Type::RealMatrix,{}},{"residual_norm",Type::Real,{}},{"scaled_residual_norm",Type::Real,{}},{"converged",Type::Integer,{}},{"status",Type::String,{}},{"message",Type::String,{}},{"iterations",Type::Integer,{}},{"evaluations",Type::Integer,{}},{"jacobian_evaluations",Type::Integer,{}},{"backend_evaluations",Type::Integer,{}},{"native_evaluations",Type::Integer,{}},{"step_norm",Type::Real,{}},{"flag",Type::Integer,{}},{"bracket",Type::RealMatrix,{}},{"method",Type::String,{}},{"jacobian",Type::String,{}},{"cache_hit",Type::Integer,{}},{"cache_key",Type::String,{}},{"requested_threads",Type::Integer,{}},{"threads_used",Type::Integer,{}}};value->schema=std::move(schema);
        return valid?Type::Record:Type::Invalid;
    }


