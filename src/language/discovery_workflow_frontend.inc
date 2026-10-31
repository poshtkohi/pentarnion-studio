    std::optional<Type> checkDiscoveryWorkflow(const ExprPtr& value){
        const auto& name=value->text;if(name!="basins"&&name!="lyapunovscan")return {};
        bool valid=true;auto require=[&](bool condition,const char* message){if(!condition){add("E2053",message,value->span);valid=false;}};
        const auto count=value->children.size();require(count==2||count==3,"Use basins/lyapunovscan(model,initial_rows[,options]).");require(!hasNamedArguments(value),"Use positional arguments and a final options record.");
        if(count>=2){require(value->children[0]->type==Type::OdeModel,"Discovery workflows require a checked ODE model.");require(value->children[1]->type==Type::RealMatrix,"Initial rows require complete real state-coordinate rows.");}if(count==3)require(value->children[2]->type==Type::Record,"Workflow settings require an options record.");
        auto schema=std::make_shared<ValueSchema>();auto fields=[&](std::initializer_list<const char*> keys,Type type){for(const auto* key:keys)schema->fields.push_back({key,type,{}});};
        if(name=="lyapunovscan"){
            auto synthetic=std::make_shared<Expr>(*value);synthetic->text="verifyexperiment";synthetic->children={count?value->children[0]:std::make_shared<Expr>(),std::make_shared<Expr>(),std::make_shared<Expr>()};synthetic->children[1]->type=Type::Record;synthetic->children[2]->type=Type::RealMatrix;(void)checkVerificationCall(synthetic);schema->fields.push_back({"verification",Type::Record,synthetic->schema});
            fields({"parameters","initials"},Type::RealMatrix);fields({"catalogue_runs"},Type::Integer);fields({"workflow"},Type::String);
        }else{
            fields({"format","messages","notice"},Type::String);fields({"initials","labels","resolved","attractor_ids","representative_run_ids","counts","fractions","conditional_fractions"},Type::RealMatrix);fields({"runs","attractors","unresolved","escaped","failures","pending","resolved_count"},Type::Integer);fields({"unresolved_fraction","escape_fraction","failure_fraction","pending_fraction"},Type::Real);
            auto synthetic=std::make_shared<Expr>(*value);synthetic->text="discover";(void)checkDiscoveryCall(synthetic);schema->fields.push_back({"source_experiment",Type::Record,synthetic->schema});
        }
        value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
