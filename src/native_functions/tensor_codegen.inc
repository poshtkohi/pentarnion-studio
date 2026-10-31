    Type tensorEntry(const Schema& schema)const{
        if(!schema)invalid("A native tensor requires an immutable entry descriptor.");
        return schema->tensorEntryType;
    }
    std::vector<std::size_t> tensorShape(const Schema& schema)const{
        if(!schema||schema->shape.empty()||schema->shape.size()>8)invalid("Native tensor rank must be1..8 with a fixed positive shape.");
        const auto width=language::coordinateWidth(Type::Tensor,schema);
        if(!width||width>1024)invalid("Every native tensor value or temporary must fit the1024-coordinate local allowance.");
        std::size_t cells=1;for(const auto extent:schema->shape){if(!extent||extent>1024||cells>1024/extent)invalid("A native tensor exceeds its bounded fixed shape.");cells*=extent;}
        return schema->shape;
    }
    std::size_t tensorCells(const Schema& schema)const{const auto dimensions=tensorShape(schema);return std::accumulate(dimensions.begin(),dimensions.end(),std::size_t(1),std::multiplies<>{});}
    int tensorTag(const Schema& schema)const{const auto entry=tensorEntry(schema);if(entry==Type::Real)return -1;if(entry==Type::Pentarnion)return -2;if(entry==Type::Complex||entry==Type::Element)return static_cast<int>(tag(entry,schema));invalid("Unsupported native tensor entry descriptor.");}
    std::string tensorDimensions(const Schema& schema)const{std::string code;for(const auto extent:tensorShape(schema)){if(!code.empty())code+=",";code+=std::to_string(extent);}return code;}
    std::string tensorDescriptor(const Schema& schema)const{
        const auto entry=tensorEntry(schema);return std::to_string(static_cast<int>(entry))+":"+tensorDimensions(schema)+":"+
            (entry==Type::Element?elementDefinition(entry,schema)->hash():entry==Type::Complex?elementDefinition(entry,schema)->hash():entry==Type::Pentarnion?std::string("A_gamma_v1_normalized"):std::string("real"));
    }
    std::string tensorTemplate(const Schema& schema)const{return "S,"+std::to_string(tensorTag(schema))+","+tensorDimensions(schema);}
    std::size_t tensorProductWork(const Schema& schema)const{
        const auto entry=tensorEntry(schema);const auto cost=entry==Type::Element||entry==Type::Complex?
            3*elementDefinition(entry,schema)->terms().size()+nativeCoordinateWidth(entry,schema):entry==Type::Pentarnion?std::size_t(100):std::size_t(1);
        return tensorCells(schema)*std::max<std::size_t>(1,cost);
    }
    bool sameNativeTensor(const Schema& a,const Schema& b)const{
        if(!a||!b||tensorEntry(a)!=tensorEntry(b)||tensorShape(a)!=tensorShape(b))return false;
        if(tensorEntry(a)==Type::Element)return elementDefinition(Type::Element,a)->hash()==elementDefinition(Type::Element,b)->hash();
        return true;
    }
    std::size_t tensorIndex(const Schema& schema,const std::vector<ExprPtr>& selectors) {
        const auto dimensions=tensorShape(schema);if(selectors.size()!=1&&selectors.size()!=dimensions.size())invalid("Native tensor indexing requires one linear index or one integer per axis.");
        std::size_t index=0,stride=1;
        for(std::size_t axis=0;axis<selectors.size();++axis){const auto extent=selectors.size()==1?tensorCells(schema):dimensions[axis];
            const auto value=selectors[axis]->kind==language::ExprKind::EndIndex?static_cast<long long>(extent):literal(selectors[axis],static_cast<long long>(extent));
            if(value<1||static_cast<std::size_t>(value)>extent)invalid("Native tensor indices must be in-bounds positive integer literals or end.",selectors[axis]->span);
            index+=(static_cast<std::size_t>(value)-1)*stride;stride*=extent;
        }return index;
    }
    std::string tensorConstruct(const Expr& e,std::size_t depth){
        if(e.children.size()<2||e.children.size()>3||e.type!=Type::Tensor)invalid("Use tensor(values,[literal_shape][,entry_kind]).",e.span);
        const auto schema=e.schema;const auto& source=*e.children[0];const auto input=expr(e.children[0],depth+1);const auto targetTag=tensorTag(schema);
        if(source.type==Type::Tensor){if(tensorEntry(source.schema)!=tensorEntry(schema)||tensorTag(source.schema)!=targetTag||tensorCells(source.schema)!=tensorCells(schema))invalid("Native tensor reshape preserves its complete entry algebra and element count.",e.span);return "tensor_reshape<"+tensorDimensions(schema)+">("+input+")";}
        const auto entry=tensorEntry(schema);
        if(array(source.type)){
            const auto sourceSchema=arraySchema(source);const auto [rows,columns]=shape(sourceSchema);const auto sourceTag=arrayTag(source.type,sourceSchema);
            const bool explicitCoordinates=e.children.size()==3&&source.type==Type::RealMatrix;
            if(explicitCoordinates){const auto width=nativeCoordinateWidth(entry,schema);if(columns!=width||(rows!=1&&rows!=tensorCells(schema)))invalid("Native tensor coordinate matrices require N-by-complete-entry-width or one broadcast row.",e.span);return "tensor_from_coordinates<"+tensorTemplate(schema)+">("+input+")";}
            if(sourceTag!=targetTag||(tensorCells(schema)!=rows*columns&&rows*columns!=1))invalid("Native tensor construction cannot alter its inferred entry algebra or element count.",e.span);
            return "tensor_from_array<"+tensorTemplate(schema)+">("+input+")";
        }
        if(!scalar(source.type)&&source.type!=entry)invalid("Native tensor broadcast requires a real scalar or matching complete entry.",e.span);
        if(entry==Type::Element&&scalar(source.type)&&elementDefinition(entry,schema)->unit().empty()){
            const auto& initial=e.children[0];const auto zero=initial->kind==language::ExprKind::IntegerLiteral||initial->kind==language::ExprKind::RealLiteral;
            if(!zero||realLiteralForTensor(initial->text)!=0)invalid("A native nonunital tensor scalar constructor accepts only the literal zero.",e.span);
            return "TypedTensor<"+tensorTemplate(schema)+">{}";
        }
        return "tensor_fill<"+tensorTemplate(schema)+">("+promoted(entry,schema,source,input)+")";
    }
    static long double realLiteralForTensor(const std::string& text){double result=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result,std::chars_format::general);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw ode::OdeError("N2401","Invalid native tensor scalar literal.");return result;}
    std::string tensorBinary(const Expr& e,const std::string& a,const std::string& b){
        const auto& left=*e.children[0];const auto& right=*e.children[1];const bool l=left.type==Type::Tensor,r=right.type==Type::Tensor;
        const auto schema=l?left.schema:right.schema;const auto& op=e.text;charge(nativeCoordinateWidth(Type::Tensor,schema));
        if(l&&r){if(!sameNativeTensor(left.schema,right.schema))invalid("Native tensor operands differ in shape or complete entry algebra.",e.span);
            if(op=="=="||op=="!="||op=="~="){const auto equal="tensor_equal("+a+","+b+")";return op=="=="?equal:"(!"+equal+")";}
            if(op=="+"||op=="-")return "("+a+op+b+")";
            if(op==".*"){charge(tensorProductWork(schema));return "tensor_entry_product("+a+","+b+",gamma)";}
            invalid("Native tensor pairs require explicit entrywise multiplication; implicit contraction is unsupported.",e.span);
        }
        const auto& scalarValue=l?right:left;const auto& scalarCode=l?b:a;const auto& tensorCode=l?a:b;
        if((op=="/"||op=="./")&&l&&scalar(scalarValue.type))return "tensor_divide_real("+tensorCode+","+scalarCode+")";
        if((op=="*"||op==".*")&&scalar(scalarValue.type))return "tensor_scale_real("+tensorCode+","+scalarCode+")";
        if(op==".^"&&l&&right.type==Type::Integer){const auto exponent=literal(e.children[1],64);const auto entryType=tensorEntry(schema);const auto tagValue=tensorTag(schema);
            if(tagValue>=0&&exponent<0){const auto definition=elementDefinition(entryType,schema);requireInverse(definition,e.span);const auto n=definition->dimension();charge(tensorCells(schema)*n*n*n*8);}
            const auto power=tagValue>=0?"universal_power(v,"+std::to_string(exponent)+")":tagValue==-2?"cb_pent_power(v,"+std::to_string(exponent)+",gamma)":"cb_ipow(v,"+std::to_string(exponent)+")";
            charge(tensorProductWork(schema)*std::max<std::size_t>(1,static_cast<std::size_t>(std::abs(exponent))));return "tensor_map_as<"+std::to_string(tagValue)+">("+tensorCode+",[&](const auto& v){return "+power+";})";
        }
        const auto entry=promoted(tensorEntry(schema),schema,scalarValue,scalarCode);
        if(op=="+"||op=="-"){const auto code="tensor_add_element("+tensorCode+","+entry+","+(op=="-"?"true":"false")+")";return op=="-"&&!l?"(-"+code+")":code;}
        if(op=="*"||op==".*"){charge(tensorProductWork(schema));return "tensor_scale_element("+tensorCode+","+entry+",gamma,"+(l?"false":"true")+")";}
        invalid("Unsupported native tensor scalar operator.",e.span);
    }
    std::string tensorCall(const Expr& e,std::size_t depth){
        if(e.children.empty()||e.children[0]->type!=Type::Tensor)invalid("A native tensor intrinsic requires one typed tensor.",e.span);
        const auto schema=e.children[0]->schema;const auto dimensions=tensorShape(schema);const auto cells=tensorCells(schema);const auto entry=tensorEntry(schema);const auto inputTag=tensorTag(schema);const auto a=expr(e.children[0],depth+1);const auto& name=e.text;
        if(name=="size"){
            if(e.children.size()==1){std::string out="array_make<S,1,"+std::to_string(dimensions.size())+",-1>(";for(std::size_t axis=0;axis<dimensions.size();++axis){if(axis)out+=",";out+="S("+std::to_string(dimensions[axis])+")";}return out+")";}
            if(e.children.size()!=2)invalid("Use size(T[,literal_axis]).",e.span);
            const auto axis=literal(e.children[1],1000000);if(axis<1)invalid("A native tensor size axis must be positive.",e.span);return "S("+std::to_string(static_cast<std::size_t>(axis)<=dimensions.size()?dimensions[static_cast<std::size_t>(axis)-1]:1)+")";
        }
        if(name=="numel"||name=="length"||name=="ndims"){if(e.children.size()!=1)invalid("Tensor metadata intrinsic accepts one tensor.",e.span);return "S("+std::to_string(name=="numel"?cells:name=="ndims"?dimensions.size():*std::max_element(dimensions.begin(),dimensions.end()))+")";}
        if(name=="sum"||name=="mean"){
            if(e.children.size()>2||(e.children.size()==2&&(e.children[1]->kind!=language::ExprKind::StringLiteral||e.children[1]->text!="all")))invalid("Use sum/mean(T[,\"all\"]).",e.span);
            return "([&](){const auto input="+a+";auto total=tensor_element(input,0);for(unsigned n=1;n<"+std::to_string(cells)+";++n)total=total+tensor_element(input,n);return "+(name=="mean"?"total*S(1.0L/"+std::to_string(cells)+".0L)":std::string("total"))+";}())";
        }
        if(name=="norm"){if(e.children.size()>2||(e.children.size()==2&&(e.children[1]->kind!=language::ExprKind::StringLiteral||e.children[1]->text!="fro")))invalid("Use norm(T[,\"fro\"]).",e.span);return "tensor_norm("+a+")";}
        if(name=="tensorcoords"||name=="tensorvalues"){if(e.children.size()!=1)invalid("Tensor coordinate extraction accepts one tensor.",e.span);return "tensor_coordinates("+a+")";}
        if(name=="coeff"){if(e.children.size()!=2)invalid("Use coeff(T,literal_index0).",e.span);const auto index=literal(e.children[1],nativeCoordinateWidth(entry,schema)-1);if(index<0)invalid("Tensor coefficients use zero-based positive coordinates.",e.span);return "tensor_coefficient("+a+","+std::to_string(index)+")";}
        if(e.children.size()!=1)invalid("Entrywise native tensor functions accept one tensor.",e.span);
        std::string mapped;const std::map<std::string,unsigned> whole{{"sin",1},{"cos",2},{"exp",3},{"sinh",4},{"cosh",5}};
        if(name=="conj")mapped=inputTag==-1?"v":inputTag==-2?"conjugate(v)":"universal_conjugate(v)";
        else if(const auto found=whole.find(name);found!=whole.end()){
            if(inputTag>=0){requireWhole(elementDefinition(entry,schema),e.span);mapped="universal_whole(v,"+std::to_string(found->second)+")";charge(cells*(768*elementDefinition(entry,schema)->terms().size()+1024*elementDefinition(entry,schema)->dimension()));}
            else if(inputTag==-2)mapped="cb_whole(v,"+std::to_string(found->second)+")";
            else mapped="cb_"+name+"(v)";
        }else invalid("Unsupported native tensor intrinsic: "+name,e.span);
        return "tensor_map_as<"+std::to_string(inputTag)+">("+a+",[&](const auto& v){return "+mapped+";})";
    }
