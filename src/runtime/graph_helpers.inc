    using GraphFields=std::map<std::string,Value>;
    struct GraphInput {
        std::size_t nodes=0,edgeCount=0;bool directed=false,multigraph=false;
        std::vector<uint64_t> endpoints;std::vector<double> weights;
    };
    std::string graphText(const Value& value,SourceSpan span) {
        const auto* text=std::get_if<std::string>(&value);if(!text)fail("R2150","Graph option requires a string.",span);return *text;
    }
    const GraphFields* graphOptions(const std::vector<Value>& arguments,std::size_t required,
                                    std::initializer_list<const char*> allowed,SourceSpan span) {
        if(arguments.size()<required||arguments.size()>required+1)fail("R2150","Incorrect graph argument count; see help.",span);
        if(arguments.size()==required)return nullptr;
        const auto* fields=recordFields(arguments.back());if(!fields)fail("R2150","Graph options require a typed record.",span);
        for(const auto& [key,value]:*fields){(void)value;if(std::none_of(allowed.begin(),allowed.end(),[&](const auto* name){return key==name;}))fail("R2150","Unknown graph option: "+key,span);}
        return fields;
    }
    const Value* graphOption(const GraphFields* fields,const char* key) const {
        if(!fields)return nullptr;
        const auto found=fields->find(key);return found==fields->end()?nullptr:&found->second;
    }
    I graphInteger(const Value& value,I low,I high,SourceSpan span) {
        const auto number=integer(value,span);if(number<low||number>high)fail("R2150","Graph integer option lies outside its permitted range.",span);return number;
    }
    GraphInput graphInput(const Value& value,SourceSpan span) {
        const auto* fields=recordFields(value);
        if(!fields||fields->size()!=8||!fields->contains("format")||graphText(fields->at("format"),span)!="PentarnionGraph-v1")fail("R2150","Expected an immutable graph/digraph record.",span);
        for(const auto* key:{"nodes","edge_count","directed","multigraph","edges","weights","labels"})if(!fields->contains(key))fail("R2150","Graph record is incomplete.",span);
        GraphInput input;input.nodes=static_cast<std::size_t>(graphInteger(fields->at("nodes"),0,100000,span));input.edgeCount=static_cast<std::size_t>(graphInteger(fields->at("edge_count"),0,500000,span));
        input.directed=graphInteger(fields->at("directed"),0,1,span)!=0;input.multigraph=graphInteger(fields->at("multigraph"),0,1,span)!=0;
        const auto edges=numericArray(fields->at("edges"),span);
        if(edges.components!=1||edges.rows!=input.edgeCount||edges.columns!=2)fail("R2150","Graph edge matrix must have edge_count rows and two real columns.",span);
        input.endpoints.reserve(edges.values.size());
        for(const auto endpoint:edges.values){if(endpoint<1||endpoint>static_cast<double>(input.nodes)||std::floor(endpoint)!=endpoint)fail("R2150","Graph node IDs must be integers in 1..nodes.",span);input.endpoints.push_back(static_cast<uint64_t>(endpoint));}
        input.weights=numericVector(fields->at("weights"),span);
        if(input.weights.size()!=input.edgeCount)fail("R2150","Graph weight vector must match edge_count.",span);
        const auto* labels=recordFields(fields->at("labels"));if(!labels)fail("R2150","Graph labels require a typed record keyed n1,n2,... .",span);
        for(const auto& [key,label]:*labels){
            if(key.size()<2||key[0]!='n'||key[1]=='0'||!std::all_of(key.begin()+1,key.end(),[](unsigned char c){return c>='0'&&c<='9';}))fail("R2150","Node label keys must be n1,n2,... .",span);
            uint64_t id=0;for(std::size_t k=1;k<key.size();++k){if(id>100000)fail("R2150","Node label ID is out of range.",span);id=10*id+static_cast<unsigned char>(key[k]-'0');}
            if(id<1||id>input.nodes)fail("R2150","Node label ID is outside 1..nodes.",span);
            (void)graphText(label,span);
        }
        return input;
    }
    graph::Result graphRun(const GraphInput& input,penta_graph_request_v1 request,
                           std::size_t capacity,SourceSpan span) {
        if(capacity>options_.maxMatrixCoordinates)fail("R1005","Graph result exceeds the coordinate allowance.",span);
        request.struct_size=sizeof(request);request.nodes=input.nodes;request.edges=input.edgeCount;
        request.directed=input.directed;request.multigraph=input.multigraph;
        request.endpoints=input.endpoints.data();request.weights=input.weights.data();request.max_work=options_.maxNativeOperations;
        chargeNumericWork(1,span);
        return graph::Module(options_.numericModuleDirectory).execute(request,capacity,options_.cancellation,
            [this,span](std::size_t){chargeNumericWork(1,span);});
    }
    std::optional<Value> graphCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        static const std::set<std::string> names{"graph","digraph","graphinfo","graphcomponents","components","shortestpath","spanningtree","toposort","centrality","graphlaplacian","graphadjacency","graphlayout","graphcycle","plotgraph"};
        if(!names.contains(name))return {};
        try {
            if(name=="graph"||name=="digraph") {
                if(arguments.size()>2)fail("R2150","Use graph/digraph(edge_matrix[,options]).",span);
                const auto* settings=graphOptions(arguments,arguments.empty()?0:1,{"n","weights","multigraph","labels"},span);
                GraphInput input;input.directed=name=="digraph";
                const auto edges=arguments.empty()?NumericArray{}:numericArray(arguments[0],span);
                if(edges.components!=1||(!edges.values.empty()&&edges.columns!=2))fail("R2150","Graph edges require a real matrix with two columns.",span);
                input.edgeCount=edges.values.empty()?0:edges.rows;
                if(input.edgeCount>500000)fail("R2150","Graph edge count exceeds500000.",span);
                std::vector<double> storedEdges=edges.values;
                for(const auto endpoint:storedEdges){if(endpoint<1||endpoint>100000||std::floor(endpoint)!=endpoint)fail("R2150","Graph node IDs must be positive integers no larger than100000.",span);input.endpoints.push_back(static_cast<uint64_t>(endpoint));input.nodes=std::max(input.nodes,static_cast<std::size_t>(endpoint));}
                if(const auto* count=graphOption(settings,"n")){const auto n=static_cast<std::size_t>(graphInteger(*count,0,100000,span));if(n<input.nodes)fail("R2150","options(n=...) must include every edge endpoint.",span);input.nodes=n;}
                if(const auto* multiple=graphOption(settings,"multigraph"))input.multigraph=graphInteger(*multiple,0,1,span)!=0;
                if(const auto* weights=graphOption(settings,"weights")){input.weights=numericVector(*weights,span);if(input.weights.size()==1&&input.edgeCount!=1)input.weights.assign(input.edgeCount,input.weights[0]);if(input.weights.size()!=input.edgeCount)fail("R2150","Graph weights require one finite real value per edge, or a scalar broadcast.",span);}
                else input.weights.assign(input.edgeCount,1);
                Value labels=graphOption(settings,"labels")?*graphOption(settings,"labels"):makeRecord({},span);
                Value result=makeRecord({{"format",std::string("PentarnionGraph-v1")},{"nodes",I(input.nodes)},{"edge_count",I(input.edgeCount)},{"directed",I(input.directed)},{"multigraph",I(input.multigraph)},
                    {"edges",numericResult(storedEdges,input.edgeCount,2,1,span)},{"weights",numericResult(input.weights,input.edgeCount,1,1,span)},{"labels",std::move(labels)}},span);
                input=graphInput(result,span);penta_graph_request_v1 request{};request.operation=PENTA_GRAPH_INFO;(void)graphRun(input,request,4*input.nodes,span);return result;
            }
            if(arguments.empty())fail("R2150","Graph command requires a graph/digraph record.",span);
            auto input=graphInput(arguments[0],span);penta_graph_request_v1 request{};const GraphFields* settings=nullptr;
            if(name=="shortestpath") {
                settings=graphOptions(arguments,3,{"method"},span);request.operation=PENTA_GRAPH_SHORTEST_PATH;
                request.source=static_cast<uint64_t>(graphInteger(arguments[1],1,static_cast<I>(input.nodes),span));request.target=static_cast<uint64_t>(graphInteger(arguments[2],1,static_cast<I>(input.nodes),span));
                std::string method="auto";if(const auto* item=graphOption(settings,"method"))method=graphText(*item,span);
                if(method!="auto"&&method!="dijkstra"&&method!="bellman_ford")fail("R2150","Shortest-path method must be auto,dijkstra or bellman_ford.",span);
                request.kind=method=="auto"?0:method=="dijkstra"?1:2;
                const auto result=graphRun(input,request,2*input.nodes,span);const auto nodes=static_cast<std::size_t>(result.report.extra[0]),edges=static_cast<std::size_t>(result.report.extra[1]);
                const std::vector<double> nodeIds(result.values.begin(),result.values.begin()+static_cast<std::ptrdiff_t>(nodes)),edgeIds(result.values.begin()+static_cast<std::ptrdiff_t>(nodes),result.values.end());
                return makeRecord({{"path",numericResult(nodeIds,1,nodes,1,span)},{"edge_ids",numericResult(edgeIds,1,edges,1,span)},{"distance",result.report.scalar},{"reachable",I(result.report.outcome==PENTA_GRAPH_COMPLETE)},{"distance_available",I(result.report.outcome==PENTA_GRAPH_COMPLETE)},{"negative_cycle",I(result.report.outcome==PENTA_GRAPH_NEGATIVE_CYCLE)},{"status",std::string(result.report.status)},{"method",std::string(result.report.method)}},span);
            }
            if(name=="graphcomponents"||name=="components") {
                settings=graphOptions(arguments,1,{"kind"},span);request.operation=PENTA_GRAPH_COMPONENTS;std::string kind="weak";if(const auto* item=graphOption(settings,"kind"))kind=graphText(*item,span);
                if(kind!="weak"&&kind!="strong")fail("R2150","Components kind must be weak or strong.",span);
                request.kind=kind=="strong";
                const auto result=graphRun(input,request,input.nodes,span);return makeRecord({{"labels",numericResult(result.values,input.nodes,1,1,span)},{"count",I(result.report.extra[0])},{"kind",kind},{"status",std::string(result.report.status)}},span);
            }
            if(name=="centrality") {
                settings=graphOptions(arguments,1,{"kind","normalized","weighted"},span);request.operation=PENTA_GRAPH_CENTRALITY;
                std::string kind="degree";if(const auto* item=graphOption(settings,"kind"))kind=graphText(*item,span);
                const std::vector<std::string> kinds{"degree","in_degree","out_degree","strength","betweenness","closeness","harmonic"};const auto found=std::find(kinds.begin(),kinds.end(),kind);if(found==kinds.end())fail("R2150","Unknown centrality kind.",span);request.kind=static_cast<uint32_t>(found-kinds.begin());
                request.normalized=graphOption(settings,"normalized")?static_cast<uint32_t>(graphInteger(*graphOption(settings,"normalized"),0,1,span)):0;
                request.reserved=graphOption(settings,"weighted")?static_cast<uint32_t>(graphInteger(*graphOption(settings,"weighted"),0,1,span)):1;
                const auto result=graphRun(input,request,input.nodes,span);return makeRecord({{"scores",numericResult(result.values,input.nodes,1,1,span)},{"kind",kind},{"normalized",I(request.normalized)},{"weighted",I(request.reserved)},{"status",std::string(result.report.status)}},span);
            }
            if(name=="graphlayout"||name=="plotgraph") {
                request.operation=PENTA_GRAPH_LAYOUT;request.dimensions=2;request.radius=1;
                NumericArray positions;
                if(name=="graphlayout"){
                    settings=graphOptions(arguments,1,{"dimensions","radius"},span);
                    if(const auto* dimension=graphOption(settings,"dimensions"))request.dimensions=static_cast<uint32_t>(graphInteger(*dimension,2,3,span));
                    if(const auto* radius=graphOption(settings,"radius"))request.radius=finite(real(*radius,span),span);
                }else if(arguments.size()>2)fail("R2150","Use plotgraph(G[,node_positions]).",span);
                if(name=="plotgraph"&&arguments.size()==2){positions=numericArray(arguments[1],span);if(positions.components!=1||positions.rows!=input.nodes||(positions.columns!=2&&positions.columns!=3))fail("R2150","Node positions require nodes real rows with two or three columns.",span);request.dimensions=static_cast<uint32_t>(positions.columns);penta_graph_request_v1 validation{};validation.operation=PENTA_GRAPH_INFO;(void)graphRun(input,validation,4*input.nodes,span);}
                else {const auto result=graphRun(input,request,input.nodes*request.dimensions,span);positions.rows=input.nodes;positions.columns=request.dimensions;positions.values=result.values;}
                if(name=="graphlayout")return numericResult(positions.values,positions.rows,positions.columns,1,span);
                if(!input.nodes)fail("R2150","Plotting a graph requires at least one node.",span);
                const auto pointCount=input.nodes*(request.dimensions==3?7u:5u)+(input.directed?2*input.edgeCount:0);
                if(pointCount>options_.maxPlotPoints-plotPoints_)fail("R1005","Graph plot exceeds its point allowance; plot a smaller graph.",span);
                auto action=figureAction(FigureActionKind::Plot,span);action.dimensions=static_cast<int>(request.dimensions);action.axes={-1,-1,-1};PlotMesh mesh;mesh.label="graph node ID";
                double extent=0;std::array<double,3> low{},high{};
                for(std::size_t node=0;node<input.nodes;++node){std::array<double,3> point{};for(std::size_t axis=0;axis<request.dimensions;++axis){point[axis]=positions.values[node*request.dimensions+axis];if(node==0)low[axis]=high[axis]=point[axis];else{low[axis]=std::min(low[axis],point[axis]);high[axis]=std::max(high[axis],point[axis]);}}mesh.points.push_back(point);mesh.scalars.push_back(static_cast<double>(node+1));}
                for(std::size_t axis=0;axis<request.dimensions;++axis)extent=std::max(extent,high[axis]-low[axis]);
                if(!std::isfinite(extent))fail("R2150","Graph plot coordinate span exceeds finite range.",span);
                if(extent==0)extent=1;
                auto addPoint=[&](std::array<double,3> point,double id){for(auto& coordinate:point)coordinate=finite(coordinate,span);const auto index=static_cast<uint32_t>(mesh.points.size());mesh.points.push_back(point);mesh.scalars.push_back(id);return index;};
                for(std::size_t node=0;node<input.nodes;++node)for(std::size_t axis=0;axis<request.dimensions;++axis){auto a=mesh.points[node],b=a;a[axis]-=.015*extent;b[axis]+=.015*extent;const auto first=addPoint(a,static_cast<double>(node+1)),last=addPoint(b,static_cast<double>(node+1));mesh.segments.push_back({first,last});}
                for(std::size_t edge=0;edge<input.edgeCount;++edge){const auto u=static_cast<uint32_t>(input.endpoints[2*edge]-1),v=static_cast<uint32_t>(input.endpoints[2*edge+1]-1);mesh.segments.push_back({u,v});
                    if(input.directed&&u!=v){const auto a=mesh.points[u],b=mesh.points[v];std::array<double,3> direction{},normal{};double length=0;for(std::size_t axis=0;axis<3;++axis){direction[axis]=b[axis]-a[axis];length=std::hypot(length,direction[axis]);}if(length==0)continue;for(auto& value:direction)value/=length;
                        if(request.dimensions==2)normal={-direction[1],direction[0],0};else{std::size_t axis=0;for(std::size_t j=1;j<3;++j)if(std::abs(direction[j])<std::abs(direction[axis]))axis=j;std::array<double,3> basis{};basis[axis]=1;normal={direction[1]*basis[2]-direction[2]*basis[1],direction[2]*basis[0]-direction[0]*basis[2],direction[0]*basis[1]-direction[1]*basis[0]};const double magnitude=std::hypot(normal[0],normal[1],normal[2]);for(auto& value:normal)value/=magnitude;}
                        const double head=std::min(.08*extent,.3*length);std::array<double,3> left=b,right=b;for(std::size_t axis=0;axis<3;++axis){left[axis]-=head*direction[axis]-.5*head*normal[axis];right[axis]-=head*direction[axis]+.5*head*normal[axis];}const auto l=addPoint(left,static_cast<double>(v+1)),r=addPoint(right,static_cast<double>(v+1));mesh.segments.push_back({v,l});mesh.segments.push_back({v,r});
                    }
                }
                chargeNumericWork(input.nodes+input.edgeCount,span);reservePlotPoints(mesh.points.size(),span);action.mesh=std::move(mesh);result_.figureActions.push_back(std::move(action));return I(0);
            }
            graphOptions(arguments,1,{},span);
            if(name=="graphinfo")request.operation=PENTA_GRAPH_INFO;
            else if(name=="spanningtree")request.operation=PENTA_GRAPH_SPANNING_TREE;
            else if(name=="toposort")request.operation=PENTA_GRAPH_TOPOSORT;
            else if(name=="graphcycle")request.operation=PENTA_GRAPH_CYCLE;
            else request.operation=name=="graphlaplacian"?PENTA_GRAPH_LAPLACIAN:PENTA_GRAPH_ADJACENCY;
            const bool dense=name=="graphlaplacian"||name=="graphadjacency";
            if(dense&&input.nodes&&input.nodes>options_.maxMatrixElements/input.nodes)fail("R1005","Dense graph matrix exceeds the matrix allowance; use a smaller graph.",span);
            const auto capacity=dense?input.nodes*input.nodes:name=="graphinfo"?4*input.nodes:name=="graphcycle"?2*input.nodes+2:input.nodes;
            const auto result=graphRun(input,request,capacity,span);
            if(dense)return numericResult(result.values,input.nodes,input.nodes,1,span);
            if(name=="graphinfo"){
                std::array<std::vector<double>,4> columns;for(auto& column:columns)column.resize(input.nodes);for(std::size_t node=0;node<input.nodes;++node)for(std::size_t column=0;column<4;++column)columns[column][node]=result.values[4*node+column];
                return makeRecord({{"nodes",I(input.nodes)},{"edge_count",I(input.edgeCount)},{"directed",I(input.directed)},{"multigraph",I(input.multigraph)},{"self_loops",I(result.report.extra[0])},{"negative_edges",I(result.report.extra[1])},{"component_count",I(result.report.extra[2])},{"total_weight",result.report.scalar},
                    {"degree",numericResult(columns[0],input.nodes,1,1,span)},{"in_degree",numericResult(columns[1],input.nodes,1,1,span)},{"out_degree",numericResult(columns[2],input.nodes,1,1,span)},{"strength",numericResult(columns[3],input.nodes,1,1,span)},{"threads_used",I(1)},{"work_units",I(result.report.work_units)},{"status",std::string(result.report.status)}},span);
            }
            if(name=="toposort")return makeRecord({{"order",numericResult(result.values,1,result.values.size(),1,span)},{"acyclic",I(result.report.outcome==PENTA_GRAPH_COMPLETE)},{"status",std::string(result.report.status)}},span);
            if(name=="graphcycle"){
                const auto nodeCount=static_cast<std::size_t>(result.report.extra[0]),edgeCount=static_cast<std::size_t>(result.report.extra[1]);const std::vector<double> nodes(result.values.begin(),result.values.begin()+static_cast<std::ptrdiff_t>(nodeCount)),edges(result.values.begin()+static_cast<std::ptrdiff_t>(nodeCount),result.values.end());
                return makeRecord({{"nodes",numericResult(nodes,1,nodeCount,1,span)},{"edge_ids",numericResult(edges,1,edgeCount,1,span)},{"found",I(result.report.extra[2])},{"status",std::string(result.report.status)}},span);
            }
            std::vector<double> edges,weights;for(const auto value:result.values){const auto edge=static_cast<std::size_t>(value)-1;edges.push_back(static_cast<double>(input.endpoints[2*edge]));edges.push_back(static_cast<double>(input.endpoints[2*edge+1]));weights.push_back(input.weights[edge]);}
            return makeRecord({{"edge_ids",numericResult(result.values,1,result.values.size(),1,span)},{"edges",numericResult(edges,result.values.size(),2,1,span)},{"weights",numericResult(weights,result.values.size(),1,1,span)},{"total_weight",result.report.scalar},{"component_count",I(result.report.extra[0])},{"spanning",I(result.report.outcome==PENTA_GRAPH_COMPLETE)},{"status",std::string(result.report.status)}},span);
        }catch(const graph::Error& error){fail(error.status==PENTA_NUMERIC_CANCELLED?"R1006":error.status==PENTA_GRAPH_WORK_LIMIT?"R1005":"R2150",error.what(),span);}
    }
