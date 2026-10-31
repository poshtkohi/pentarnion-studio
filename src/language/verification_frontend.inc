    std::optional<Type> checkVerificationCall(const ExprPtr& value){
        const auto& name=value->text;if(name!="verifyexperiment"&&name!="verifyrun")return {};
        bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2052",text,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Verification requires language0.2.");require(!hasNamedArguments(value),"Use positional verification arguments and a final typed options record.");
        const auto count=value->children.size();require(count==3||(name=="verifyexperiment"&&count==4),"Use verifyexperiment(model,experiment,run_ids[,options]) or verifyrun(model,verification,row).");
        if(count>=2)require(value->children[0]->type==Type::OdeModel&&value->children[1]->type==Type::Record,"Verification needs the original declared ODE and a data record.");
        if(count>=3)require(name=="verifyrun"?value->children[2]->type==Type::Integer:scalarType(value->children[2]->type)||matrixType(value->children[2]->type)||rangeType(value->children[2]->type),"Select integer one-based verification rows/run IDs.");
        if(count==4)require(value->children[3]->type==Type::Record,"Verification options require a typed record.");
        auto schema=std::make_shared<ValueSchema>();auto fields=[&](std::initializer_list<const char*> keys,Type type){for(const auto* key:keys)schema->fields.push_back({key,type,{}});};
        if(name=="verifyrun"){
            fields({"t","values"},Type::RealMatrix);fields({"gamma"},Type::Real);fields({"run_id","coordinates","steps","rhs_evaluations","native_operations","cache_hit"},Type::Integer);fields({"cache_key","status","state_names","state_widths"},Type::String);
        }else{schema->fields.push_back({"restored_tiers",Type::Integer,{}});schema->fields.push_back({"checkpoint_ok",Type::Integer,{}});schema->fields.push_back({"checkpoint_message",Type::String,{}});
            fields({"format","cache_key","physical_cache_key","request_hash","source_request_hash","source_hash","labels","diagnostic_statuses","messages","status","notice"},Type::String);
            fields({"version","rows","completed","selected","tiers","physical_coordinates","coordinates","cache_hit","requested_threads","threads_used"},Type::Integer);fields({"elapsed_seconds"},Type::Real);
            fields({"failure_reason_codes","near_zero_exponents","quasiperiodic_candidate","run_ids","tier_ids","status_codes","screen_codes","gammas","t0","t1","rtols","atols","sample_steps","stop_times","maximum_norm","return_error","periods","rhs_norm","steps","evaluations","largest_lyapunov","window_drift","settings_drift","tier_drift","orthogonality_error","lyapunov_available","window_converged","settings_converged","tier_converged","positive","periodic_converged","closure_residuals","neutral_errors","stable","unstable","qr_intervals","states","exponents","reference_exponents","floquet_moduli"},Type::RealMatrix);
            if(count>=2)schema->fields.push_back({"source_experiment",Type::Record,value->children[1]->schema});
            auto settings=std::make_shared<ValueSchema>();for(const auto* key:{"method","jacobian"})settings->fields.push_back({key,Type::String,{}});
            for(const auto* key:{"tightening","transient","return_tolerance","equilibrium_tolerance","min_return_time","escape_radius","qr_step","reference_qr_step","minimum_observation","lyapunov_tolerance","minimum_positive","periodic_tolerance","stability_tolerance","max_seconds"})settings->fields.push_back({key,Type::Real,{}});
            for(const auto* key:{"compare_method","lyapunov","refine_periodic","max_intervals","periodic_iterations","max_steps","max_samples"})settings->fields.push_back({key,Type::Integer,{}});
            schema->fields.push_back({"settings",Type::Record,settings});
        }
        value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
