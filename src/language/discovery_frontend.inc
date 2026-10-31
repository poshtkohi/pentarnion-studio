#include "discovery_workflow_frontend.inc"
    std::optional<Type> checkDiscoveryCall(const ExprPtr& value) {
        const auto& name=value->text;
        if(auto workflow=checkDiscoveryWorkflow(value))return workflow;
        if(name=="exportexperiment"){
            bool valid=true;auto require=[&](bool ok,const char* text){if(!ok){add("E2051",text,value->span);valid=false;}};
            const auto count=value->children.size();require(count==3,"Use exportexperiment(model,experiment,options(ids=...,kinds=...)).");
            if(count>=2)require(value->children[0]->type==Type::OdeModel&&value->children[1]->type==Type::Record,"Exports require the original model and an experiment record.");
            if(count>=3)require(value->children[2]->type==Type::Record,"Export selection and budgets require a typed options record.");
            auto schema=std::make_shared<ValueSchema>();for(const auto* field:{"selected","written","restored","failures","bytes"})schema->fields.push_back({field,Type::Integer,{}});for(const auto* field:{"files","messages","manifest","status"})schema->fields.push_back({field,Type::String,{}});value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
        }
        if(name!="searchset"&&name!="searchinterval"&&name!="pentdirections"&&name!="discoverplan"&&name!="discover"&&name!="odeensemble"&&name!="discoverstatus"&&name!="experimentrun")return {};
        bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2050",text,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Discovery requires language0.2.");require(!hasNamedArguments(value),"Use positional discovery arguments and a final typed options record.");
        const auto count=value->children.size();auto schema=std::make_shared<ValueSchema>();
        auto fields=[&](std::initializer_list<const char*> names,Type type){for(const auto* field:names)schema->fields.push_back({field,type,{}});};
        if(name=="pentdirections"){require(count==0,"pentdirections() returns242 normalized signed five-coordinate directions.");return valid?Type::RealMatrix:Type::Invalid;}
        if(name=="searchset"){
            require(count==1,"searchset(values) declares a finite parameter set.");
            if(count){const auto type=value->children[0]->type;require(scalarType(type)||type==Type::Pentarnion||matrixType(type)||rangeType(type),"Search sets need numeric values.");schema->fields.push_back({"values",type,value->children[0]->schema});}
            fields({"kind","method"},Type::String);fields({"count","seed"},Type::Integer);
        }else if(name=="searchinterval"){
            require(count==3,"Use searchinterval(lower,upper,options(samples=N,method=...,seed=...)).");
            if(count>=2)require(scalarType(value->children[0]->type)&&scalarType(value->children[1]->type),"Interval endpoints must be real scalars.");
            if(count>=3)require(value->children[2]->type==Type::Record,"Continuous intervals require explicit sampling options.");
            fields({"values"},Type::RealMatrix);fields({"kind","method"},Type::String);fields({"count","seed"},Type::Integer);
        }else if(name=="discoverstatus"){
            require(count==2,"Use discoverstatus(experiment,one_based_run_id).");
            if(count>=2)require(value->children[0]->type==Type::Record&&value->children[1]->type==Type::Integer,"Status needs an experiment record and integer runID.");
            fields({"status","message","failure_reason"},Type::String);fields({"run_id","status_code","steps","evaluations","failure_reason_code"},Type::Integer);
            fields({"gamma","stop_time","maximum_norm","return_error","period","rhs_norm"},Type::Real);fields({"initial","parameters","state"},Type::RealMatrix);
        }else if(name=="experimentrun"){
            require(count==3||count==4,"Use experimentrun(model,experiment,runID[,options]).");
            if(count>=3)require(value->children[0]->type==Type::OdeModel&&value->children[1]->type==Type::Record&&value->children[2]->type==Type::Integer,"Replay requires a declared ODE, experiment record and integer runID.");
            if(count==4)require(value->children[3]->type==Type::Record,"Replay options require a typed record.");
            fields({"t","values"},Type::RealMatrix);fields({"gamma"},Type::Real);fields({"run_id","coordinates","steps","rhs_evaluations","native_operations","cache_hit"},Type::Integer);
            fields({"cache_key","status","state_names","state_widths"},Type::String);
        }else{
            require(count==2||count==3,"Use discover/odeensemble/discoverplan(model,initial_rows[,options]).");
            if(count>=2){require(value->children[0]->type==Type::OdeModel,"Discovery requires an existing declared ODE model.");require(value->children[1]->type==Type::RealMatrix||rangeType(value->children[1]->type),"Initial rows must contain complete flat real coordinates, including all five coefficients per Pentarnion state.");}
            if(count==3)require(value->children[2]->type==Type::Record,"Discovery options require a typed record.");
            fields({"runs","coordinates","physical_coordinates","parameter_count","initial_rows","gamma_count","parameter_combinations","estimated_bytes","within_budget"},Type::Integer);
            fields({"initials","parameters","gammas"},Type::RealMatrix);fields({"format","status","model_name","state_names","parameter_names","source_hash"},Type::String);
            auto settings=std::make_shared<ValueSchema>();for(const auto* field:{"t0","t1","sample_step","rtol","atol","max_seconds","escape_radius","transient","return_tolerance","equilibrium_tolerance","min_return_time"})settings->fields.push_back({field,Type::Real,{}});
            for(const auto* field:{"max_steps","max_samples"})settings->fields.push_back({field,Type::Integer,{}});
            for(const auto* field:{"method","jacobian"})settings->fields.push_back({field,Type::String,{}});
            schema->fields.push_back({"settings",Type::Record,settings});
            if(name!="discoverplan"){
                fields({"failure_reason_codes","states","status_codes","stop_times","maximum_norm","return_error","period","rhs_norm","steps","evaluations"},Type::RealMatrix);
                fields({"completed","restored","failures","thresholds","requested_threads","threads_used","cache_hit","prepared_models","checkpoint_ok","messages_truncated"},Type::Integer);
                fields({"elapsed_seconds"},Type::Real);fields({"failure_reason_labels","cache_key","request_hash","messages","checkpoint_message"},Type::String);
            }
        }
        value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
