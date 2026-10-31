    std::optional<Type> checkChaosCall(const ExprPtr& value) {
        const auto& name=value->text;
        if(name!="lyapunov"&&name!="poincare"&&name!="recurrence"&&name!="stability"&&name!="ensemble")return {};
        bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2015",text,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Dynamical analysis requires language0.2.");
        require(!hasNamedArguments(value),"Use positional analysis arguments and a final typed options record.");
        const auto count=value->children.size();const std::size_t required=(name=="poincare"||name=="recurrence")?1:2;
        require(count==required||count==required+1,"Incorrect analysis argument count; use the documented arguments and optional options record.");
        if(count<required)return Type::Invalid;
        if(count>required)require(value->children.back()->type==Type::Record,"Analysis settings must be a typed options record.");
        if(name=="lyapunov")require(value->children[0]->type==Type::OdeModel||value->children[0]->type==Type::Function,"lyapunov requires a declared ODE model or native map with compiler-generated tangent dynamics.");
        if(name=="poincare")require(value->children[0]->type==Type::Solution,"poincare requires a native ODE solution containing localized events.");
        if(name=="stability")require(value->children[0]->type==Type::OdeModel||value->children[0]->type==Type::Function,"stability requires a typed ODE model or native-compatible function.");
        if(name=="ensemble") {
            require(value->children[0]->type==Type::Function,"ensemble requires a typed native-compatible map callback.");
            require(value->children[1]->type==Type::RealMatrix||value->children[1]->type==Type::LogicalMatrix,"Ensemble initial states must be a real matrix with one independent run per row.");
        }
        if(name=="recurrence"){const auto type=value->children[0]->type;require(matrixType(type)||rangeType(type)||type==Type::RealSeries||type==Type::PentarnionSeries,"Recurrence input requires real/Pentarnion numeric sample rows.");}
        if(name=="lyapunov"||name=="stability") {const auto type=value->children[1]->type;require(scalarType(type)||type==Type::Pentarnion||type==Type::Tuple||type==Type::RealMatrix||rangeType(type),"Analysis point must contain complete real/Pentarnion states.");}
        auto schema=std::make_shared<ValueSchema>();
        auto fields=[&](std::initializer_list<const char*> names,Type type){for(const auto* field:names)schema->fields.push_back({field,type,{}});};
        fields({"status"},Type::String);
        if(name=="lyapunov") {
            fields({"exponents","t","history","local_growth","window_difference","state"},Type::RealMatrix);
            fields({"largest","orthogonality_error","duration","transient","qr_step"},Type::Real);
            fields({"intervals","steps","rhs_evaluations","dimension","cache_hit"},Type::Integer);
            fields({"cache_key","method","jacobian","message"},Type::String);
        }else if(name=="poincare") {
            fields({"t","states","intervals","direction"},Type::RealMatrix);fields({"crossings","event"},Type::Integer);
        }else if(name=="recurrence") {
            fields({"matrix","distance","returned","index"},Type::RealMatrix);fields({"rate","radius"},Type::Real);
            fields({"lag","theiler","eligible_pairs","recurrent_pairs"},Type::Integer);
        }else if(name=="stability") {
            fields({"jacobian","values_real","values_imag","measure"},Type::RealMatrix);fields({"residual_norm"},Type::Real);
            fields({"equilibrium","stable","unstable","inconclusive"},Type::Integer);fields({"kind"},Type::String);
        }else {
            fields({"states","success","escaped","iterations","evaluations"},Type::RealMatrix);fields({"messages"},Type::String);
            fields({"messages_truncated","runs","failures","escapes","requested_threads","threads_used","cache_hit"},Type::Integer);fields({"cache_key","request_hash","checkpoint_message","random_parameter","distribution","rng_protocol"},Type::String);fields({"task_status"},Type::RealMatrix);fields({"completed","restored","checkpoint_ok","progress_calls","user_stopped","seed"},Type::Integer);fields({"elapsed_seconds"},Type::Real);
        }
        value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
