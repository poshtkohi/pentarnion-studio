    std::optional<Type> checkModelSensitivityCall(const ExprPtr& value){
        if(value->text!="modelsensitivity")return {};
        bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2301",text,value->span);valid=false;}};
        const auto count=value->children.size();require(m_options.languageVersion=="0.2","Native solver sensitivity requires language0.2.");require(!hasNamedArguments(value),"Use positional model/lower/upper arguments and a typed options record.");require(count==4,"Use modelsensitivity(model,lower,upper,options(initial=...,parameters=...)).");
        if(count>=1)require(value->children[0]->type==Type::Function||value->children[0]->type==Type::OdeModel,"Supply a checked ODE model or native-compatible map/reaction function.");
        for(std::size_t k=1;k<std::min<std::size_t>(3,count);++k)require(matrixType(value->children[k]->type)||rangeType(value->children[k]->type),"Sensitivity bounds require real coordinate vectors.");
        if(count>=4)require(value->children[3]->type==Type::Record,"Solver sensitivity requires an options record.");
        auto schema=std::make_shared<ValueSchema>();auto fields=[&](std::initializer_list<const char*> names,Type type){for(const auto* field:names)schema->fields.push_back({field,type,{}});};
        fields({"kind","summary","method","distribution","estimator","uncertainty","cache_key","request_hash","parameter_names","state_names","summary_grid","status","effect_scale"},Type::String);
        fields({"coefficient","node","samples","dimensions","seed","bootstrap","levels","evaluations","native_operations","cache_hit","threads_requested","threads_granted","threads_used","peak_active_threads","parallel_calls","maximum_bytes","estimated_per_evaluation_bytes","estimated_concurrent_bytes","budget_workers"},Type::Integer);fields({"confidence","elapsed_seconds","gamma","mean","variance"},Type::Real);
        fields({"lower","upper","initial","parameter_values","state_widths","parameter_coordinates","x","y","mu","mu_star","sigma","first_order","total_order","first_lower","first_upper","total_lower","total_upper"},Type::RealMatrix);
        // Solver and parameter-layout fields are runtime dependent data-only
        // records, inspectable via display/get_option without inventing a shape.
        fields({"solver","parameter_layout"},Type::Record);value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
