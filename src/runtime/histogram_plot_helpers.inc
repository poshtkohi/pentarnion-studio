    std::optional<Value> histogramPlotCall(const Expr& expression,const std::vector<Value>& arguments){
        const auto& name=expression.text;const auto span=expression.span;if(name!="bar"&&name!="histplot")return {};
        if(arguments.empty()||arguments.size()>3)fail("R2212","Use bar(histogram[,options]), bar(y[,options]), bar(x,y[,options]) or histplot(samples,edges[,options]).",span);
        const std::map<std::string,Value>* settings=nullptr;std::size_t count=arguments.size();
        if(const auto* fields=recordFields(arguments.back());fields&&!(count==1&&fields->contains("counts"))){settings=fields;--count;for(const auto& [key,value]:*fields){(void)value;if(key!="width"&&key!="baseline"&&key!="density")fail("R2212","Unknown bar/histogram display setting: "+key,span);}}
        auto option=[&](const char* key)->const Value*{if(!settings)return nullptr;const auto it=settings->find(key);return it==settings->end()?nullptr:&it->second;};
        const double width=option("width")?real(*option("width"),span):name=="histplot"?1.:.8,baseline=option("baseline")?real(*option("baseline"),span):0.;const auto density=option("density")?integer(*option("density"),span):0;
        if(width<=0||width>1||density<0||density>1)fail("R2212","Bar width must lie in(0,1] and density must be0 or1.",span);
        std::vector<double> heights,left,right;Value histogram;
        if(name=="histplot"){
            if(count!=2)fail("R2212","histplot requires samples and explicit bin edges.",span);
            Expr checked=expression;checked.text="histogram";const auto data=scienceDataCall(checked,{arguments[0],arguments[1]});if(!data)fail("R2212","The native histogram data adapter is unavailable.",span);histogram=*data;
        }else if(count==1&&recordFields(arguments[0]))histogram=arguments[0];
        if(recordFields(histogram)){
            const auto& fields=*recordFields(histogram);if(!fields.contains("edges")||!fields.contains(density?"density":"counts"))fail("R2212","A histogram plot requires retained edges/counts/density fields.",span);
            const auto edges=numericVector(fields.at("edges"),span,false);heights=numericVector(fields.at(density?"density":"counts"),span,false);if(edges.size()!=heights.size()+1)fail("R2212","Histogram edges must bracket each retained bin exactly once.",span);
            for(std::size_t k=0;k<heights.size();++k){if(edges[k+1]<=edges[k]||heights[k]<0)fail("R2212","Histogram edges must increase and heights must be nonnegative.",span);const auto center=edges[k]*.5+edges[k+1]*.5,half=(edges[k+1]-edges[k])*.5*width;left.push_back(center-half);right.push_back(center+half);}
        }else{
            if(name!="bar"||(count!=1&&count!=2)||density)fail("R2212","Numeric bar plots accept one y vector or explicit x,y vectors; density applies to histogram data.",span);
            heights=numericVector(arguments[count-1],span,false);std::vector<double> x;if(count==2)x=numericVector(arguments[0],span,false);else for(std::size_t k=0;k<heights.size();++k)x.push_back(k+1.);
            if(x.size()!=heights.size())fail("R2212","Bar positions/heights must have matching lengths.",span);
            for(std::size_t k=0;k<x.size();++k){if(k&&x[k]<=x[k-1])fail("R2212","Explicit bar positions must be strictly increasing.",span);const double spacing=x.size()==1?1.:k==0?x[1]-x[0]:k+1==x.size()?x[k]-x[k-1]:std::min(x[k]-x[k-1],x[k+1]-x[k]);left.push_back(x[k]-.5*spacing*width);right.push_back(x[k]+.5*spacing*width);}
        }
        if(heights.empty()||heights.size()>4096||heights.size()>options_.maxPlotPoints/4||heights.size()*4>options_.maxPlotPoints-std::min(options_.maxPlotPoints,plotPoints_))fail("R1005","Bar plots require1..4096 bins within the aggregate plot-point allowance.",span);
        PlotMesh mesh;mesh.label=name=="histplot"?"Histogram":"Bars";for(std::size_t k=0;k<heights.size();++k){tick(span);const auto at=static_cast<std::uint32_t>(mesh.points.size());mesh.points.insert(mesh.points.end(),{{left[k],baseline,0},{right[k],baseline,0},{right[k],heights[k],0},{left[k],heights[k],0}});mesh.triangles.push_back({at,at+1,at+2});mesh.triangles.push_back({at,at+2,at+3});mesh.scalars.insert(mesh.scalars.end(),4,.5);}
        auto action=figureAction(FigureActionKind::Plot,span);action.dimensions=2;action.axes={-1,-1,-1};action.mesh=std::move(mesh);result_.figureActions.push_back(std::move(action));plotPoints_+=heights.size()*4;return I(0);
    }
