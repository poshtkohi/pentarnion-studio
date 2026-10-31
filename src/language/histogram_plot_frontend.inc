    std::optional<Type> checkHistogramPlotCall(const ExprPtr& value){
        const auto& name=value->text;if(name!="bar"&&name!="histplot")return {};
        bool valid=true;auto require=[&](bool yes,const char* text){if(!yes){add("E2212",text,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Bar/histogram displays require language0.2.");require(!hasNamedArguments(value),"Use positional values and an optional final settings record.");const auto n=value->children.size();require(n>=1&&n<=3,"Use bar(y[,options]), bar(x,y[,options]), bar(histogram[,options]) or histplot(samples,edges[,options]).");
        if(name=="histplot"){require(n==2||n==3,"histplot requires samples and explicit bin edges.");for(std::size_t k=0;k<std::min<std::size_t>(2,n);++k)require(value->children[k]->type==Type::RealMatrix||rangeType(value->children[k]->type),"Histogram samples/edges require real vectors.");}
        else if(n)require(value->children[0]->type==Type::RealMatrix||rangeType(value->children[0]->type)||value->children[0]->type==Type::Record,"Bars require a real vector or histogram record.");
        if(n==3)require(value->children[2]->type==Type::Record,"Bar display settings require a record.");
        return valid?Type::Integer:Type::Invalid;
    }
