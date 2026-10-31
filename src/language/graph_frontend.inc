    std::optional<Type> checkGraphCall(const ExprPtr& value) {
        const auto& name=value->text;
        static const std::set<std::string> names{"graph","digraph","graphinfo","graphcomponents","components","shortestpath","spanningtree","toposort","centrality","graphlaplacian","graphadjacency","graphlayout","graphcycle","plotgraph"};
        if(!names.contains(name))return {};
        bool valid=true;auto require=[&](bool okay,const char* message){if(!okay){add("E2150",message,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Graph functions require language0.2.");require(!hasNamedArguments(value),"Use positional graph arguments and typed option records.");
        const auto count=value->children.size();auto schema=std::make_shared<ValueSchema>();
        auto fields=[&](std::initializer_list<const char*> names,Type type){for(const auto* field:names)schema->fields.push_back({field,type,{}});};
        if(name=="graph"||name=="digraph"){
            require(count<=2,"Use graph/digraph(edge_matrix[,options]).");
            if(count)require(value->children[0]->type==Type::RealMatrix||value->children[0]->type==Type::LogicalMatrix,"Graph edges require a real two-column matrix.");
            if(count>1)require(value->children[1]->type==Type::Record,"Graph options require a typed record.");
            fields({"format"},Type::String);fields({"nodes","edge_count","directed","multigraph"},Type::Integer);fields({"edges","weights"},Type::RealMatrix);
            std::shared_ptr<const ValueSchema> labels=std::make_shared<ValueSchema>();
            if(count>1&&value->children[1]->schema)for(const auto& field:value->children[1]->schema->fields)if(field.name=="labels")labels=field.schema;
            schema->fields.push_back({"labels",Type::Record,std::move(labels)});value->schema=schema;return valid?Type::Record:Type::Invalid;
        }
        const std::size_t required=name=="shortestpath"?3:1;
        const bool options=name=="graphcomponents"||name=="components"||name=="shortestpath"||name=="centrality"||name=="graphlayout";
        require(count==required||((options||name=="plotgraph")&&count==required+1),"Incorrect graph command argument count.");
        if(count<required)return Type::Invalid;
        require(value->children[0]->type==Type::Record,"Graph command requires a graph/digraph record.");
        if(options&&count>required)require(value->children.back()->type==Type::Record,"Graph options require a typed record.");
        if(name=="shortestpath")require(value->children[1]->type==Type::Integer&&value->children[2]->type==Type::Integer,"Shortest-path endpoints require integer node IDs.");
        if(name=="plotgraph"){
            if(count==2)require(value->children[1]->type==Type::RealMatrix,"Graph positions require a real matrix.");
            return valid?Type::Void:Type::Invalid;
        }
        if(name=="graphlayout"||name=="graphlaplacian"||name=="graphadjacency")return valid?Type::RealMatrix:Type::Invalid;
        fields({"status"},Type::String);
        if(name=="graphinfo"){
            fields({"nodes","edge_count","directed","multigraph","self_loops","negative_edges","component_count","threads_used","work_units"},Type::Integer);
            fields({"total_weight"},Type::Real);fields({"degree","in_degree","out_degree","strength"},Type::RealMatrix);
        }else if(name=="graphcomponents"||name=="components"){
            fields({"labels"},Type::RealMatrix);fields({"count"},Type::Integer);fields({"kind"},Type::String);
        }else if(name=="shortestpath"){
            fields({"path","edge_ids"},Type::RealMatrix);fields({"distance"},Type::Real);fields({"reachable","distance_available","negative_cycle"},Type::Integer);fields({"method"},Type::String);
        }else if(name=="spanningtree"){
            fields({"edge_ids","edges","weights"},Type::RealMatrix);fields({"total_weight"},Type::Real);fields({"component_count","spanning"},Type::Integer);
        }else if(name=="toposort"){
            fields({"order"},Type::RealMatrix);fields({"acyclic"},Type::Integer);
        }else if(name=="graphcycle"){
            fields({"nodes","edge_ids"},Type::RealMatrix);fields({"found"},Type::Integer);
        }else {
            fields({"scores"},Type::RealMatrix);fields({"kind"},Type::String);fields({"normalized","weighted"},Type::Integer);
        }
        value->schema=schema;return valid?Type::Record:Type::Invalid;
    }
