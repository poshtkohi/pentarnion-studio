    std::optional<Type> checkThirdDerivativeCall(const ExprPtr& value){
        if(value->text!="thirdderivative")return {};
        bool valid=true;auto require=[&](bool okay,const char* message){if(!okay){add("E2301",message,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Native third derivatives require language0.2.");
        require(!hasNamedArguments(value),"Use thirdderivative(function,point,u,v,w[,options(parameters=...)]).");
        const auto count=value->children.size();require(count==5||count==6,"Third derivatives require a function, point, and three directions, with optional trailing options.");
        if(count<5)return Type::Invalid;
        require(value->children[0]->type==Type::Function,"Third derivatives require a typed function handle.");
        const auto point=value->children[1]->type;require(scalarType(point)||point==Type::Pentarnion||point==Type::Element||point==Type::ElementMatrix||point==Type::Tensor||point==Type::Complex||matrixType(point)||rangeType(point)||point==Type::Tuple||point==Type::Invalid,"Callback point requires complete real coordinates, a fixed array/algebra element or a numeric tuple.");
        for(unsigned n=2;n<5;++n){const auto type=value->children[n]->type;require(scalarType(type)||type==Type::Pentarnion||type==Type::Element||type==Type::Complex||type==Type::Tensor||universalArrayType(type)||rangeType(type)||type==Type::Invalid,"Third-derivative directions require real coordinates or one complete matching algebra/fixed-array direction.");}
        if(count==6)require(value->children[5]->type==Type::Record||value->children[5]->type==Type::Invalid,"Third-derivative options require a typed record.");
        const auto function=value->children[0]->function?value->children[0]->function:value->children[0]->schema?value->children[0]->schema->function:nullptr;
        if(function){
            if(!checkNativeSolverPoint(value->children[1],*function,"E2301"))valid=false;
            for(std::size_t n=0;n<function->parameters.size();++n){const auto& parameter=function->parameters[n];const auto schema=n<function->parameterSchemas.size()?function->parameterSchemas[n]:parameter.schema;require(nativeSolverScalar(parameter.type,schema),"Compiled third-derivative parameters require numeric scalar elements or explicitly shaped native arrays.");}
            const auto results=function->result==Type::Tuple?function->results:std::vector<Type>{function->result};
            for(std::size_t n=0;n<results.size();++n){const auto type=results[n];const auto schema=function->result==Type::Tuple?(function->resultSchema&&n<function->resultSchema->elements.size()?function->resultSchema->elements[n].schema:nullptr):function->resultSchema;require(nativeSolverScalar(type,schema),"Third-derivative callbacks return numeric scalar elements or explicitly shaped arrays, including typed tuples.");}
        }
        auto schema=std::make_shared<ValueSchema>();
        for(const auto* field:{"values","third","u","v","w","active_coordinates","output_widths"})schema->fields.push_back({field,Type::RealMatrix,{}});
        for(const auto* field:{"input_coordinates","total_input_coordinates","output_coordinates","native_evaluations","threads_used","cache_hit"})schema->fields.push_back({field,Type::Integer,{}});
        for(const auto* field:{"cache_key","method","algebra"})schema->fields.push_back({field,Type::String,{}});
        schema->fields.push_back({"gamma",Type::Real,{}});value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
