    std::optional<Type> checkChartGeometryCall(const ExprPtr& value){
        const auto& name=value->text;const bool path=name=="geodesic";if(name!="chartgeometry"&&!path)return {};
        bool valid=true;auto require=[&](bool okay,const char* message){if(!okay){add("E2200",message,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Chart geometry requires language 0.2.");require(!hasNamedArguments(value),"Use positional geometry arguments and an options record.");
        const std::size_t mandatory=path?3u:2u;const auto count=value->children.size();require(count==mandatory||count==mandatory+1,"Use chartgeometry(embedding,[u,v][,options]) or geodesic(embedding,[u,v],[du,dv][,options]).");if(count<mandatory)return Type::Invalid;
        require(value->children[0]->type==Type::Function,"An embedding requires a native-compatible typed function.");
        for(std::size_t i=1;i<mandatory;++i)require(matrixType(value->children[i]->type)||rangeType(value->children[i]->type),"Chart points and velocities require real two-coordinate vectors.");
        if(count>mandatory)require(value->children.back()->type==Type::Record,"Chart geometry options require a record.");
        const auto function=value->children[0]->function?value->children[0]->function:value->children[0]->schema?value->children[0]->schema->function:nullptr;
        if(function){require(function->parameters.size()>=2,"An embedding begins (real u,real v[,parameters]).");if(function->parameters.size()>=2)require(function->parameters[0].type==Type::Real&&function->parameters[1].type==Type::Real,"Chart coordinates are real parameters.");require(function->result==Type::Tuple||function->result==Type::Pentarnion,"An embedding returns a real coordinate tuple or a Pentarnion.");}
        auto schema=std::make_shared<ValueSchema>();
        if(path){
            for(const auto field:{"t","coordinates","velocity","positions","transport","speed_squared","transport_norm_squared"})schema->fields.push_back({field,Type::RealMatrix,{}});
            for(const auto field:{"transport_enabled","steps","evaluations","embedding_dimension","cache_hit","native_operations","threads_used"})schema->fields.push_back({field,Type::Integer,{}});
            for(const auto field:{"max_relative_speed_drift","max_relative_transport_drift","elapsed_seconds"})schema->fields.push_back({field,Type::Real,{}});
            for(const auto field:{"integrator","connection","metric_kind","derivative_method","cache_key","metric_cache_key","status"})schema->fields.push_back({field,Type::String,{}});
        }else{
            for(const auto field:{"point","coordinates","tangent","metric","inverse_metric","christoffel","riemann","ricci","tangent_projector","normal_projector","second_fundamental","mean_curvature_vector"})schema->fields.push_back({field,Type::RealMatrix,{}});
            for(const auto field:{"dimension","embedding_dimension","extrinsic_available","cache_hit","native_operations","threads_used"})schema->fields.push_back({field,Type::Integer,{}});
            for(const auto field:{"gaussian_curvature","scalar_curvature","area_density"})schema->fields.push_back({field,Type::Real,{}});
            for(const auto field:{"metric_kind","derivative_method","cache_key","metric_cache_key"})schema->fields.push_back({field,Type::String,{}});
        }
        value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
