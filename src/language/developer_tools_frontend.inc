    std::shared_ptr<const ValueSchema> moduleInfoSchema()const {
        auto schema=std::make_shared<ValueSchema>();schema->fields={{"name",Type::String,{}},{"path",Type::String,{}},{"available",Type::Integer,{}},{"loaded",Type::Integer,{}},{"abi",Type::Integer,{}},{"status",Type::String,{}}};return schema;
    }
    std::optional<Type> checkDeveloperToolsCall(const ExprPtr& value){
        if(value->text!="module_info"&&value->text!="module_load")return {};
        const auto count=value->children.size();bool valid=m_options.languageVersion=="0.2"&&!hasNamedArguments(value);
        if((value->text=="module_load"&&count!=1)||(value->text=="module_info"&&count>1))valid=false;
        if(count&&value->children[0]->type!=Type::String)valid=false;
        if(!valid){add("E2100","Use module_info(), module_info(name), or module_load(name) with a catalogue module name.",value->span);return Type::Invalid;}
        if(count)value->schema=moduleInfoSchema();
        else{auto schema=std::make_shared<ValueSchema>();for(const auto* name:{"numerics","linalg","roots","fit","opt","maps","pde","pde_experiments","fem","chaos","discovery","orbits","bifurcation","graph","graph_tools","networks","geometry","sensitivity","dae","mechanics","units","nd","inference","evolution","science","petsc","cas","vector_plot"})schema->fields.push_back({name,Type::Record,moduleInfoSchema()});value->schema=schema;}
        return Type::Record;
    }
