    std::optional<Type> checkGeometryCall(const ExprPtr& value){
        const auto& n=value->text;if(n!="surf"&&n!="imagesc"&&n!="contour"&&n!="isosurface"&&n!="curvegeometry")return {};
        bool valid=true;auto require=[&](bool b,const char* m){if(!b){add("E2110",m,value->span);valid=false;}};require(m_options.languageVersion=="0.2","Geometry functions require language 0.2.");require(!hasNamedArguments(value),"Geometry commands use positional arguments.");
        const auto count=value->children.size();if(n=="curvegeometry"){require(count==1,"Use curvegeometry(points).");if(count)require(value->children[0]->type==Type::RealMatrix,"Curve points require a real matrix.");auto s=std::make_shared<ValueSchema>();for(const auto f:{"arc","tangent","normal","curvature"})s->fields.push_back({f,Type::RealMatrix,{}});s->fields.push_back({"length",Type::Real,{}});s->fields.push_back({"status",Type::String,{}});value->schema=s;return valid?Type::Record:Type::Invalid;}
        require(n=="isosurface"?count==5:n=="contour"?(count==3||count==4):count==3,"Use surf/imagesc(x,y,Z), contour(x,y,Z[,levels]) or isosurface(x,y,z,values,level).");
        for(const auto& child:value->children)require(scalarType(child->type)||child->type==Type::RealMatrix||rangeType(child->type),"Geometry samples/axes/levels must be real numeric quantities.");
        return valid?Type::Void:Type::Invalid;
    }
