    std::shared_ptr<ValueSchema> iterationInfoSchema() {
        auto schema=std::make_shared<ValueSchema>();
        for(const auto* name:{"iteration","funccount","start_index","dimension","iteration_available","objective_available","residual_available","step_available","accepted","history_dropped"})schema->fields.push_back({name,Type::Integer,{}});
        for(const auto* name:{"objective","residual_norm","step_norm","elapsed_seconds"})schema->fields.push_back({name,Type::Real,{}});
        for(const auto* name:{"event","state","history_columns"})schema->fields.push_back({name,Type::String,{}});
        schema->fields.push_back({"history",Type::RealMatrix,{}});return schema;
    }
    std::optional<Type> checkIterationCall(const ExprPtr& value) {
        if(value->text!="iterationinfo")return {};
        bool valid=true;if(m_options.languageVersion!="0.2"){add("E2050","iterationinfo requires language0.2.",value->span);valid=false;}
        if(!value->children.empty()){add("E2050","Use iterationinfo() without arguments as the OutputFcn metadata template.",value->span);valid=false;}
        value->schema=iterationInfoSchema();return valid?Type::Record:Type::Invalid;
    }
    bool checkIterationOptions(const ExprPtr& options) {
        if(!options||!options->schema)return true;
        bool valid=true;
        for(const auto& field:options->schema->fields){
            if(field.name=="OutputEvery"&&field.type!=Type::Integer){add("E2050","OutputEvery must be a positive Integer.",options->span);valid=false;}
            if(field.name!="OutputFcn")continue;
            if(field.type!=Type::Function){add("E2050","OutputFcn requires a function handle.",options->span);valid=false;continue;}
            const auto function=field.schema?field.schema->function:nullptr;
            if(function&&(function->parameters.size()!=3||function->parameters[0].type!=Type::RealMatrix||function->parameters[1].type!=Type::Record||function->parameters[2].type!=Type::String||(function->result!=Type::Integer&&function->result!=Type::Real))){add("E2050","OutputFcn signature: integer monitor(matrix x,record optimValues=iterationinfo(),string state=\"iter\"); return0/1 to continue/stop.",options->span);valid=false;}
        }
        return valid;
    }
