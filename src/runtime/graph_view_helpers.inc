    runtime::PlotGraph graphViewInput(const Value& value,const Value* positions,const GraphFields* settings,SourceSpan span){
        const auto input=graphInput(value,span);runtime::PlotGraph graph;graph.directed=input.directed;graph.multigraph=input.multigraph;graph.weights=input.weights;graph.labels.resize(input.nodes);
        for(std::size_t edge=0;edge<input.edgeCount;++edge)graph.edges.push_back({static_cast<std::uint32_t>(input.endpoints[2*edge]),static_cast<std::uint32_t>(input.endpoints[2*edge+1])});
        const auto* fields=recordFields(value);const auto* labels=recordFields(fields->at("labels"));for(const auto& [key,label]:*labels){const auto id=static_cast<std::size_t>(std::stoull(key.substr(1)));graph.labels[id-1]=graphText(label,span);}
        if(const auto* v=graphOption(settings,"dimensions"))graph.dimensions=static_cast<int>(graphInteger(*v,2,3,span));
        if(const auto* v=graphOption(settings,"radius"))graph.radius=finite(real(*v,span),span);
        if(const auto* v=graphOption(settings,"layout"))graph.layout=graphText(*v,span);
        if(const auto* v=graphOption(settings,"seed"))graph.seed=static_cast<std::uint64_t>(graphInteger(*v,0,4294967295ll,span));
        if(positions){const auto data=numericArray(*positions,span);if(data.components!=1||data.rows!=input.nodes||(data.columns!=2&&data.columns!=3))fail("R2151","Graph positions require one real row per stable node ID and two/three columns.",span);graph.dimensions=static_cast<int>(data.columns);graph.layout="manual";graph.positions.resize(input.nodes);for(std::size_t node=0;node<input.nodes;++node)for(std::size_t d=0;d<data.columns;++d)graph.positions[node][d]=data.values[node*data.columns+d];}
        else graph.positions=plotting::graphPositions(input.nodes,graph.dimensions,graph.layout,graph.radius,graph.seed,options_.cancellation);
        graph.interactive=input.nodes<=plotting::graphInteractiveNodes&&input.edgeCount<=plotting::graphInteractiveEdges;graph.nodeLabels=input.nodes<=128;
        if(const auto* v=graphOption(settings,"interactive"))graph.interactive=graphInteger(*v,0,1,span)!=0;
        if(const auto* v=graphOption(settings,"node_labels"))graph.nodeLabels=graphInteger(*v,0,1,span)!=0;
        if(const auto* v=graphOption(settings,"edge_labels"))graph.edgeLabels=graphInteger(*v,0,1,span)!=0;
        if(graph.nodeLabels&&input.nodes>plotting::graphInteractiveNodes)fail("R2151","Node labels are bounded to 1024 displayed nodes; disable node_labels for a static large graph.",span);
        if(graph.edgeLabels&&input.edgeCount>plotting::graphInteractiveEdges)fail("R2151","Edge labels are bounded to 8192 displayed edges.",span);
        if(const auto* v=graphOption(settings,"node_size"))graph.nodeSize=finite(real(*v,span),span);
        if(const auto* v=graphOption(settings,"edge_width"))graph.edgeWidth=finite(real(*v,span),span);
        auto ids=[&](const Value& value,std::size_t maximum){const auto numbers=numericVector(value,span);std::vector<std::uint32_t> result;for(double id:numbers){if(!std::isfinite(id)||id<1||id>static_cast<double>(maximum)||std::floor(id)!=id)fail("R2151","Graph highlight/path IDs must reference existing stable identities.",span);result.push_back(static_cast<std::uint32_t>(id));}return result;};
        if(const auto* v=graphOption(settings,"path"))plotting::highlightGraphPath(graph,ids(*v,input.nodes));
        if(const auto* v=graphOption(settings,"selected_nodes"))graph.selectedNodes=ids(*v,input.nodes);
        if(const auto* v=graphOption(settings,"selected_edges"))graph.selectedEdges=ids(*v,input.edgeCount);
        if(const auto* v=graphOption(settings,"highlight_nodes"))graph.highlightedNodes=ids(*v,input.nodes);
        if(const auto* v=graphOption(settings,"highlight_edges"))graph.highlightedEdges=ids(*v,input.edgeCount);
        plotting::validateGraphView(graph);penta_graph_request_v1 validation{};validation.operation=PENTA_GRAPH_INFO;(void)graphRun(input,validation,4*input.nodes,span);return graph;
    }
    std::optional<Value> graphViewCall(const Expr& expression,const std::vector<Value>& arguments){
        const auto& name=expression.text;const auto span=expression.span;if(name!="plotgraph"&&name!="graphlayout"&&name!="savegraphlayout"&&name!="loadgraphlayout")return {};
        try{
            if(arguments.empty())fail("R2151","Graph view command requires the original graph record.",span);
            if(name=="loadgraphlayout"){
                if(arguments.size()!=2)fail("R2151","Use loadgraphlayout(G,path).",span);
                const auto graph=graphViewInput(arguments[0],nullptr,nullptr,span);const auto path=dataPath(graphText(arguments[1],span),span);dataio::Options io;io.maxFileBytes=16*1024*1024;io.cancellation=options_.cancellation;const auto loaded=plotting::graphLayoutFromJson(dataio::readBytes(path,io),graph);
                std::vector<double> positions,nodes,edges,highlightedNodes,highlightedEdges;for(const auto& p:loaded.positions)for(int d=0;d<loaded.dimensions;++d)positions.push_back(p[static_cast<std::size_t>(d)]);for(auto id:loaded.selectedNodes)nodes.push_back(id);for(auto id:loaded.selectedEdges)edges.push_back(id);for(auto id:loaded.highlightedNodes)highlightedNodes.push_back(id);for(auto id:loaded.highlightedEdges)highlightedEdges.push_back(id);
                return makeRecord({{"positions",numericResult(positions,loaded.positions.size(),loaded.dimensions,1,span)},{"selected_nodes",numericResult(nodes,1,nodes.size(),1,span)},{"selected_edges",numericResult(edges,1,edges.size(),1,span)},{"highlighted_nodes",numericResult(highlightedNodes,1,highlightedNodes.size(),1,span)},{"highlighted_edges",numericResult(highlightedEdges,1,highlightedEdges.size(),1,span)},{"dimensions",I(loaded.dimensions)},{"seed",I(loaded.seed)},{"radius",loaded.radius},{"layout",loaded.layout},{"status",std::string("loaded")}},span);
            }
            const Value* positions=nullptr;const GraphFields* settings=nullptr;std::string path;
            if(name=="savegraphlayout"){
                if(arguments.size()<3||arguments.size()>4)fail("R2151","Use savegraphlayout(G,positions,path[,options]).",span);
                positions=&arguments[1];path=graphText(arguments[2],span);if(arguments.size()==4)settings=recordFields(arguments[3]);if(arguments.size()==4&&!settings)fail("R2151","Layout settings require a typed record.",span);
            }else if(name=="graphlayout"){
                if(arguments.size()>2)fail("R2151","Use graphlayout(G[,options(layout,dimensions,radius,seed)]).",span);
                if(arguments.size()==2){settings=recordFields(arguments[1]);if(!settings)fail("R2151","Graph layout settings require a typed record.",span);}
            }else{
                if(arguments.size()>3)fail("R2151","Use plotgraph(G[,positions][,options]).",span);
                if(arguments.size()>=2){if(recordFields(arguments[1]))settings=recordFields(arguments[1]);else positions=&arguments[1];}if(arguments.size()==3){if(settings)fail("R2151","Supply only one graph options record after optional positions.",span);settings=recordFields(arguments[2]);if(!settings)fail("R2151","Graph view settings require a typed record.",span);}
            }
            const std::set<std::string> allowed{"layout","dimensions","radius","seed","interactive","node_labels","edge_labels","node_size","edge_width","path","highlight_nodes","highlight_edges","selected_nodes","selected_edges","overwrite"};if(settings)for(const auto& [key,value]:*settings){(void)value;if(!allowed.contains(key))fail("R2151","Unknown graph view option: "+key,span);}
            if(name=="graphlayout"&&(!settings||(!settings->contains("layout")&&!settings->contains("seed"))))return {};
            auto graph=graphViewInput(arguments[0],positions,settings,span);
            if(name=="graphlayout"){std::vector<double> points;for(const auto& p:graph.positions)for(int d=0;d<graph.dimensions;++d)points.push_back(p[static_cast<std::size_t>(d)]);return numericResult(points,graph.positions.size(),graph.dimensions,1,span);}
            if(name=="savegraphlayout"){const auto target=dataPath(path,span);const bool overwrite=graphOption(settings,"overwrite")&&graphInteger(*graphOption(settings,"overwrite"),0,1,span)!=0;if(!overwrite&&std::filesystem::exists(target))fail("R2151","Graph layout exists; enable overwrite explicitly.",span);dataio::Options io;io.maxFileBytes=16*1024*1024;io.cancellation=options_.cancellation;dataio::writeBytesAtomic(target,plotting::graphLayoutJson(graph),io);return I(0);}
            if(graph.positions.empty())fail("R2151","Plotting requires at least one graph node.",span);
            const auto estimate=graph.positions.size()+2*plotting::graphSegments(graph).size();reservePlotPoints(estimate,span);chargeNumericWork(estimate,span);auto action=figureAction(FigureActionKind::Plot,span);action.dimensions=graph.dimensions;action.axes={-1,-1,-1};action.graph=std::move(graph);result_.figureActions.push_back(std::move(action));return I(0);
        }catch(const plotting::ExportError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R2151",error.what(),span);}
        catch(const dataio::Error& error){fail("R2151",error.what(),span);}
    }
