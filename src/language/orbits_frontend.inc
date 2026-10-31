    std::optional<Type> checkOrbitsCall(const ExprPtr& value) {
        const auto& name=value->text;
        if(name=="branchplot"){bool valid=true;if(value->children.empty()||value->children.size()>2||value->children[0]->type!=Type::Record||(value->children.size()==2&&value->children[1]->type!=Type::Record)){add("E2055","Use branchplot(continuation_report[,options]).",value->span);valid=false;}return valid?Type::Integer:Type::Invalid;}
        if(name!="periodicorbit"&&name!="continueequilibrium"&&name!="continueperiodic")return {};
        bool valid=true;auto require=[&](bool condition,const char* text){if(!condition){add("E2015",text,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Orbit analysis requires language 0.2.");
        require(!hasNamedArguments(value),"Use positional model and initial state followed by a typed options record.");
        const auto count=value->children.size();require(count==2||count==3,"Use model, initial state and optional typed settings.");
        if(count<2)return Type::Invalid;
        require(value->children[0]->type==Type::OdeModel,"Orbit analysis requires a declared autonomous ODE model.");
        const auto initial=value->children[1]->type;
        require(scalarType(initial)||initial==Type::Pentarnion||initial==Type::Element||initial==Type::Complex||initial==Type::ElementMatrix||initial==Type::Tuple||initial==Type::RealMatrix||rangeType(initial),"Initial state must contain all real or Pentarnion ODE coordinates.");
        if(count>2)require(value->children[2]->type==Type::Record,"Orbit analysis settings require a typed options record.");
        auto schema=std::make_shared<ValueSchema>();auto fields=[&](std::initializer_list<const char*> names,Type type){for(const auto* field:names)schema->fields.push_back({field,type,{}});};
        fields({"status","message","cache_key"},Type::String);
        if(name=="periodicorbit"){
            fields({"x","t","states","monodromy","multipliers_real","multipliers_imag","multipliers_modulus","history"},Type::RealMatrix);
            fields({"period","closure_residual","scaled_closure_residual","phase_residual","neutral_error","amplitude","scaled_amplitude"},Type::Real);
            fields({"converged","stable","unstable","inconclusive","neutral_index","iterations","flow_evaluations","steps","rhs_evaluations","dimension","cache_hit","fundamental_period_verified"},Type::Integer);
            fields({"method","jacobian"},Type::String);
        }else{
            fields({"x","parameter","tangent","residuals","step_sizes","values_real","values_imag","measure","stable","unstable","inconclusive","fold_candidate","hopf_candidate","period_doubling_candidate","stability_change","period","closure_residuals","neutral_errors"},Type::RealMatrix);
            fields({"rows","dimension","physical_dimension","evaluations","iterations","steps","completed","cache_hit","bifurcation_refined"},Type::Integer);
            fields({"kind","parameter_name","parameter_derivative"},Type::String);auto markers=std::make_shared<ValueSchema>();for(const auto* key:{"source_run_id","requested","rows","accepted_count","omitted"})markers->fields.push_back({key,Type::Integer,{}});for(const auto* key:{"x","kind_codes","candidate_rows","bracket_first","bracket_last","parameter","frequency_or_period","residuals","accepted"})markers->fields.push_back({key,Type::RealMatrix,{}});for(const auto* key:{"source_request_hash","statuses","messages","request_hashes","source_cache_key","notice"})markers->fields.push_back({key,Type::String,{}});schema->fields.push_back({"markers",Type::Record,markers});
        }
        value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
