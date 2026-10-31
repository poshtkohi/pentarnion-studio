    std::optional<Type> checkDerivativeModelCall(const ExprPtr& value) {
        const auto& name=value->text;const auto count=value->children.size();const bool product=name=="jvp"||name=="vjp"||name=="hvp";
        if(name!="jacobian"&&name!="gradient"&&name!="hessian"&&name!="derivative_check"&&!product)return {};
        if(count==0||value->children[0]->type!=Type::OdeModel)return {};
        auto forwarded=std::make_shared<Expr>(*value);auto callable=std::make_shared<Expr>(*value->children[0]);callable->type=Type::Function;callable->ode.reset();callable->function.reset();callable->schema.reset();
        if(const auto& model=value->children[0]->ode){
            auto function=std::make_shared<FunctionDefinition>();function->name="rhs_"+model->name;function->native=true;function->span=value->span;function->parameters=model->states;function->parameters.insert(function->parameters.end(),model->parameters.begin(),model->parameters.end());function->parameters.push_back({model->timeName,Type::Real,value->span});
            if(model->states.size()==1)function->result=model->states.front().type;else{function->result=Type::Tuple;for(const auto& state:model->states)function->results.push_back(state.type);}
            callable->function=function;auto schema=std::make_shared<ValueSchema>();schema->function=std::move(function);callable->schema=std::move(schema);
        }
        forwarded->children[0]=std::move(callable);const auto type=checkDerivativeCall(forwarded);value->schema=forwarded->schema;return type;
    }
