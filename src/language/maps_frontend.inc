    std::optional<Type> checkMapsCall(const ExprPtr& value) {
        const auto& name=value->text;
        if(name!="mapiterate"&&name!="mapdiagram"&&name!="returnmap")return {};
        bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2010",text,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Native maps require language0.2.");
        require(!hasNamedArguments(value),"Map builtins use positional arguments and a typed options record.");
        const auto count=value->children.size();
        if(name=="returnmap") {
            require(count==1||count==2,"Use returnmap(map_record_or_samples[,options(component=1,lag=1)]).");
            if(count){const auto type=value->children[0]->type;require(type==Type::Record||matrixType(type)||rangeType(type)||type==Type::RealSeries||type==Type::PentarnionSeries,"Return-map input requires an orbit record or numeric samples.");}
            if(count==2)require(value->children[1]->type==Type::Record,"Return-map options must be a typed record.");
            auto schema=std::make_shared<ValueSchema>();for(const auto field:{"x","y","source_index","target_index"})schema->fields.push_back({field,Type::RealMatrix,{}});for(const auto field:{"component","lag","samples"})schema->fields.push_back({field,Type::Integer,{}});value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
        }
        const bool diagram=name=="mapdiagram";const std::size_t required=diagram?4:2;
        require(count==required||count==required+1,diagram?"Use mapdiagram(map,initial,\"parameter\",values[,options]).":"Use mapiterate(map,initial[,options]).");
        if(count<2)return Type::Invalid;
        require(value->children[0]->type==Type::Function,"Map iteration requires a typed native-compatible function handle.");
        const auto function=value->children[0]->function?value->children[0]->function:value->children[0]->schema?value->children[0]->schema->function:nullptr;
        if(function){require(!function->parameters.empty(),"A map requires at least one state argument.");for(std::size_t n=0;n<function->parameters.size();++n)require(nativeSolverScalar(function->parameters[n].type,nativeSolverParameterSchema(*function,n)),"Compiled map parameters require numeric scalars or explicitly shaped native arrays.");const auto types=function->result==Type::Tuple?function->results:std::vector<Type>{function->result};for(std::size_t n=0;n<types.size();++n){const auto shape=function->result==Type::Tuple?(function->resultSchema&&n<function->resultSchema->elements.size()?function->resultSchema->elements[n].schema:nullptr):function->resultSchema;require(nativeSolverScalar(types[n],shape),"A native map returns numeric scalar elements or explicitly shaped arrays.");}}
        const auto point=value->children[1];Type xType=Type::Real;std::shared_ptr<const ValueSchema> xSchema;bool pentOrbit=false;
        if(point->type==Type::PentarnionMatrix)require(solverArrayPoint(function),"A whole Pentarnion matrix map point requires an explicitly shaped matrix parameter.");
        require(scalarType(point->type)||point->type==Type::Pentarnion||point->type==Type::Element||point->type==Type::ElementMatrix||point->type==Type::Tensor||point->type==Type::Complex||point->type==Type::Tuple||matrixType(point->type)||rangeType(point->type),"Initial map states require numeric scalars, a real vector or typed tuple.");
        
        xType=solverPointType(point,function,xSchema);pentOrbit=xType==Type::Pentarnion;
        if(function&&!checkNativeSolverPoint(point,*function,"E2010"))valid=false;
        if(count>required)require(value->children.back()->type==Type::Record,"Map options require a typed record.");
        if(diagram&&count>=4){require(value->children[2]->type==Type::String,"The scanned map parameter is a trailing real parameter name string.");const auto type=value->children[3]->type;require((matrixType(type)&&type!=Type::PentarnionMatrix)||rangeType(type),"Map diagram values require a nonempty real vector.");}
        auto schema=std::make_shared<ValueSchema>();schema->fields={{"x",xType,xSchema},{"states",Type::RealMatrix,{}},{"orbit",pentOrbit?Type::PentarnionMatrix:Type::RealMatrix,{}},{"n",Type::RealMatrix,{}},{"jacobian",Type::RealMatrix,{}}};
        for(const auto field:{"iterations","evaluations","jacobian_evaluations","native_evaluations","samples","escaped","escape_iteration","cache_hit","requested_threads","threads_used"})schema->fields.push_back({field,Type::Integer,{}});
        for(const auto field:{"status","message","cache_key","jacobian_method"})schema->fields.push_back({field,Type::String,{}});
        if(diagram){for(const auto field:{"parameter","scan_index","scan_values","final_states","escaped_flags","escape_iterations"})schema->fields.push_back({field,Type::RealMatrix,{}});schema->fields.push_back({"parameter_name",Type::String,{}});}
        value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
