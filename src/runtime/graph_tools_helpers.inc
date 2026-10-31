    std::optional<Value> graphToolsCall(const Expr& expression,const std::vector<Value>& arguments){
        const auto& name=expression.text;const auto span=expression.span;if(name!="graphsparse"&&name!="graphgen")return {};
        try{
            penta_graph_tools_request_v1 request{};request.struct_size=sizeof(request);request.max_work=options_.maxNativeOperations;
            if(name=="graphsparse"){
                const auto* settings=graphOptions(arguments,1,{"kind","weighted"},span);const auto input=graphInput(arguments[0],span);request.operation=PENTA_GRAPH_TOOLS_SPARSE;request.nodes=input.nodes;request.edges=input.edgeCount;request.directed=input.directed;request.multigraph=input.multigraph;request.endpoints=input.endpoints.data();request.weights=input.weights.data();
                std::string kind="laplacian";
                if(const auto* v=graphOption(settings,"kind"))kind=graphText(*v,span);
                if(kind!="adjacency"&&kind!="incidence"&&kind!="laplacian")fail("R2152","Graph sparse kind must be adjacency, incidence or laplacian.",span);
                request.kind=kind=="adjacency"?PENTA_GRAPH_SPARSE_ADJACENCY:kind=="incidence"?PENTA_GRAPH_SPARSE_INCIDENCE:PENTA_GRAPH_SPARSE_LAPLACIAN;
                if(const auto* v=graphOption(settings,"weighted"))request.weighted=static_cast<uint32_t>(graphInteger(*v,0,1,span));
                const auto columns=kind=="incidence"?input.edgeCount:input.nodes,capacity=2*input.edgeCount+(kind=="laplacian"?input.nodes:0);if(columns>99999||3*capacity+columns+1>options_.maxMatrixCoordinates)fail("R1005","Graph CSC layout exceeds canonical dimensions or the retained coordinate allowance.",span);
                const auto result=graph_tools::Module(options_.numericModuleDirectory).execute(request,capacity,columns+1,0,options_.cancellation,[this,span](std::size_t count){chargeNumericWork(count,span);});LinalgSparseInput sparse;sparse.rows=result.report.rows;sparse.columns=result.report.columns;sparse.width=1;sparse.indices=result.indices;sparse.pointers=result.pointers;sparse.values=result.values;return linalgSparseValue(sparse,span);
            }
            const auto* settings=graphOptions(arguments,2,{"directed","seed","m","rows","columns","weight","labels"},span);const auto kind=graphText(arguments[0],span);request.operation=PENTA_GRAPH_TOOLS_GENERATE;request.nodes=static_cast<uint64_t>(graphInteger(arguments[1],0,100000,span));request.generated_weight=1;
            if(kind!="path"&&kind!="cycle"&&kind!="grid"&&kind!="gnm")fail("R2152","Graph generator must be path, cycle, grid or gnm.",span);
            request.kind=kind=="path"?PENTA_GRAPH_GENERATE_PATH:kind=="cycle"?PENTA_GRAPH_GENERATE_CYCLE:kind=="grid"?PENTA_GRAPH_GENERATE_GRID:PENTA_GRAPH_GENERATE_GNM;
            if(const auto* v=graphOption(settings,"directed"))request.directed=static_cast<uint32_t>(graphInteger(*v,0,1,span));
            if(const auto* v=graphOption(settings,"seed"))request.seed=static_cast<uint64_t>(graphInteger(*v,0,4294967295ll,span));
            if(const auto* v=graphOption(settings,"m"))request.requested_edges=static_cast<uint64_t>(graphInteger(*v,0,500000,span));
            if(const auto* v=graphOption(settings,"rows"))request.grid_rows=static_cast<uint64_t>(graphInteger(*v,1,100000,span));
            if(const auto* v=graphOption(settings,"columns"))request.grid_columns=static_cast<uint64_t>(graphInteger(*v,1,100000,span));
            if(const auto* v=graphOption(settings,"weight"))request.generated_weight=finite(real(*v,span),span);
            const auto maximum=kind=="gnm"?request.requested_edges:kind=="path"?(request.nodes?request.nodes-1:0):kind=="cycle"?request.nodes:2*request.nodes;if(3*maximum>options_.maxMatrixCoordinates)fail("R1005","Generated graph exceeds retained coordinate allowance.",span);
            const auto result=graph_tools::Module(options_.numericModuleDirectory).execute(request,0,0,2*maximum,options_.cancellation,[this,span](std::size_t count){chargeNumericWork(count,span);});std::vector<double> endpoints(result.endpoints.begin(),result.endpoints.end());Value labels=graphOption(settings,"labels")?*graphOption(settings,"labels"):makeRecord({},span);
            auto value=makeRecord({{"format",std::string("PentarnionGraph-v1")},{"nodes",I(request.nodes)},{"edge_count",I(result.report.edges)},{"directed",I(request.directed)},{"multigraph",I(0)},{"edges",numericResult(endpoints,result.report.edges,2,1,span)},{"weights",numericResult(std::vector<double>(result.report.edges,request.generated_weight),result.report.edges,1,1,span)},{"labels",std::move(labels)}},span);(void)graphInput(value,span);return value;
        }catch(const graph_tools::Error& error){fail(error.status==PENTA_NUMERIC_CANCELLED?"R1006":"R2152",error.what(),span);}
    }
