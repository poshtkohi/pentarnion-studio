    static bool universalArrayType(Type type){return type==Type::RealMatrix||type==Type::PentarnionMatrix||type==Type::ElementMatrix;}
    static Type universalArrayCellType(Type type){return type==Type::ElementMatrix?Type::Element:type==Type::PentarnionMatrix?Type::Pentarnion:Type::Real;}
    std::shared_ptr<const ValueSchema> universalArraySchema(const std::shared_ptr<const ValueSchema>& source,std::vector<std::size_t> dimensions) const {
        auto schema=source?std::make_shared<ValueSchema>(*source):std::make_shared<ValueSchema>();schema->shape=std::move(dimensions);schema->seriesElementType=Type::Invalid;return schema;
    }
    std::vector<std::size_t> universalExpressionShape(const ExprPtr& value) const {
        if(value->schema&&!value->schema->shape.empty())return value->schema->shape;
        if(value->kind==ExprKind::Group&&value->children.size()==1)return universalExpressionShape(value->children.front());
        if(value->kind==ExprKind::MatrixLiteral&&value->rows&&value->columns&&value->children.size()==value->rows*value->columns&&std::all_of(value->children.begin(),value->children.end(),[](const auto& cell){return scalarType(cell->type)||cell->type==Type::Pentarnion||cell->type==Type::Element;}))return {value->rows,value->columns};
        return {};
    }
    Type checkUniversalArrayLiteral(const ExprPtr& value) {
        const auto expected=value->schema;algebra::universal::DefinitionPtr definition=expected?expected->algebra:algebra::universal::DefinitionPtr{};
        bool valid=true,pentarnion=false,generic=static_cast<bool>(definition),scalarOnly=true;
        for(const auto& child:value->children){const auto type=check(child);scalarOnly=scalarOnly&&(scalarType(type)||type==Type::Pentarnion||type==Type::Element);
            if(type==Type::Invalid)valid=false;
            else if(type==Type::Element||type==Type::ElementMatrix){generic=true;const auto current=universalDefinition(child);if(!current||(definition&&definition->hash()!=current->hash())){add("E2540","A matrix literal cannot mix different native algebra definitions.",child->span);valid=false;}else definition=current;}
            else if((!numericType(type)&&!matrixType(type)&&!rangeType(type))||type==Type::Complex){add("E1204","Matrix literal entries must be numeric scalars, ranges or arrays of one declared algebra.",child->span);valid=false;}
            pentarnion=pentarnion||pentarnionType(type);
            if(!generic)warnWiden(child,type==Type::Pentarnion?Type::Pentarnion:Type::Real);
        }
        if(generic&&pentarnion){add("E2540","A native algebra matrix cannot mix legacy Pentarnion entries.",value->span);valid=false;}
        if(scalarOnly&&!value->rowWidths.empty()&&std::any_of(value->rowWidths.begin(),value->rowWidths.end(),[&](auto width){return width!=value->rowWidths.front();})){add("E1901","Matrix literal rows must have equal lengths.",value->span);valid=false;}
        if(scalarOnly&&expected&&!expected->shape.empty()&&expected->shape!=std::vector<std::size_t>{value->rows,value->columns}){add("E2540","Matrix literal shape differs from its explicitly declared fixed shape.",value->span);valid=false;}
        if(generic){if(!definition){add("E2540","A native algebra matrix requires an immutable entry definition.",value->span);valid=false;}else if(definition->unit().empty()&&std::any_of(value->children.begin(),value->children.end(),[](const auto& child){return scalarType(child->type)||child->type==Type::RealMatrix||rangeType(child->type);})){add("E2540","Embedding real entries in this native matrix requires an algebra unit.",value->span);valid=false;}auto schema=std::make_shared<ValueSchema>();schema->algebra=definition;if(expected&&!expected->shape.empty())schema->shape=expected->shape;else if(scalarOnly&&value->rows&&value->columns)schema->shape={value->rows,value->columns};value->schema=schema;}
        return valid?(generic?Type::ElementMatrix:pentarnion?Type::PentarnionMatrix:Type::RealMatrix):Type::Invalid;
    }
    std::optional<Type> checkUniversalArrayBinary(const ExprPtr& value,Type left,Type right) {
        const bool leftArray=universalArrayType(left),rightArray=universalArrayType(right);
        const bool generic=left==Type::ElementMatrix||right==Type::ElementMatrix||((leftArray||rightArray)&&(left==Type::Element||right==Type::Element));
        const bool fixed=(leftArray&&value->children[0]->schema&&!value->children[0]->schema->shape.empty())||(rightArray&&value->children[1]->schema&&!value->children[1]->schema->shape.empty());
        if(!generic&&!fixed)return {};
        if(!leftArray&&!rightArray)return {};
        const auto& op=value->text;bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2540",text,value->span);valid=false;}};
        auto numeric=[&](Type type){return universalArrayType(type)||scalarType(type)||type==Type::Pentarnion||type==Type::Element;};require(numeric(left)&&numeric(right),"Array arithmetic requires numeric scalar or array operands.");
        Type result=generic?Type::ElementMatrix:(left==Type::PentarnionMatrix||right==Type::PentarnionMatrix||left==Type::Pentarnion||right==Type::Pentarnion)?Type::PentarnionMatrix:Type::RealMatrix;
        std::shared_ptr<const ValueSchema> source=left==Type::ElementMatrix||left==Type::Element?value->children[0]->schema:right==Type::ElementMatrix||right==Type::Element?value->children[1]->schema:std::shared_ptr<const ValueSchema>{};
        if(generic){for(const auto& child:value->children){if(child->type==Type::Element||child->type==Type::ElementMatrix)require(child->schema&&source&&child->schema->algebra&&source->algebra&&child->schema->algebra->hash()==source->algebra->hash(),"Native matrix operands must have the same immutable entry algebra.");else require(child->type!=Type::Pentarnion&&child->type!=Type::PentarnionMatrix,"Native and legacy Pentarnion matrix entries cannot mix implicitly.");}
            const bool embedding=(!leftArray&&scalarType(left)&&(op=="+"||op=="-"))||(!rightArray&&scalarType(right)&&(op=="+"||op=="-"))||left==Type::RealMatrix||(right==Type::RealMatrix&&op!="./");
            if(embedding)require(source&&source->algebra&&!source->algebra->unit().empty(),"Real matrix/scalar entry embedding requires a validated algebra unit.");
        }
        if(op=="=="||op=="!="||op=="~=")return valid?Type::Integer:Type::Invalid;
        const auto a=leftArray?universalExpressionShape(value->children[0]):std::vector<std::size_t>{};const auto b=rightArray?universalExpressionShape(value->children[1]):std::vector<std::size_t>{};auto dimensions=!a.empty()?a:b;
        if(leftArray&&rightArray){if(op=="*"){if(a.size()==2&&b.size()==2){require(a[1]==b[0],"Fixed matrix product dimensions do not contract.");dimensions={a[0],b[1]};}else dimensions.clear();}
            else if(op=="+"||op=="-"||op==".*"||op=="./"||op==".^"){if(!a.empty()&&!b.empty())require(a==b,"Entrywise fixed matrix operations require equal shapes.");}
            else require(false,"Whole matrix equation division requires an explicit solver; use entrywise operations where intended.");}
        else require(op=="+"||op=="-"||op=="*"||op=="/"||op==".*"||op=="./"||op=="^"||op==".^","Unsupported native array/scalar operator.");
        if(op=="^"||op==".^")require(leftArray&&right==Type::Integer,"Fixed matrix powers require an integer exponent.");
        if(op=="^"&&a.size()==2)require(a[0]==a[1],"A matrix power requires a square fixed shape.");
        if(op=="/"||op=="./")require((rightArray&&right==Type::RealMatrix&&op=="./")||scalarType(right),"Array division denominators must be real scalars or explicit entrywise real arrays.");
        value->schema=universalArraySchema(source,std::move(dimensions));return valid?result:Type::Invalid;
    }
    std::optional<Type> checkUniversalArrayIndex(const ExprPtr& value,Type type) {
        if(type!=Type::ElementMatrix&&type!=Type::ArraySeries)return {};
        bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2540",text,value->span);valid=false;}};
        require(value->children.size()==2||(type==Type::ElementMatrix&&value->children.size()==3),"Use matrix(index), matrix(row,column), or array_series(sample_index).");
        bool slice=false;for(std::size_t n=1;n<value->children.size();++n){const auto index=check(value->children[n]);require(scalarType(index)||rangeType(index)||matrixType(index),"Array indices require scalar integers, ranges or colon.");slice=slice||rangeType(index)||matrixType(index);if(const auto literal=signedLiteral(value->children[n]))require(*literal>=1&&std::trunc(*literal)==*literal,"Array indices are one-based positive integers.");}
        const auto source=value->children.front()->schema;
        if(type==Type::ArraySeries){require(!slice,"Use statecoord(array_series,index0) before selecting sample ranges.");value->schema=source?universalArraySchema(source,source->shape):source;return valid&&source?source->seriesElementType:Type::Invalid;}
        value->schema=universalArraySchema(source,{});return valid?(slice?Type::ElementMatrix:Type::Element):Type::Invalid;
    }
    std::optional<Type> checkUniversalArrayCall(const ExprPtr& value) {
        const auto& name=value->text;const auto count=value->children.size();
        if(name=="statecoord"){
            if(count!=2||value->children[0]->type!=Type::ArraySeries||value->children[1]->type!=Type::Integer){add("E2540","Use statecoord(array_state_series,zero_based_coordinate).",value->span);return Type::Invalid;}
            const auto schema=value->children[0]->schema;const auto width=schema?coordinateWidth(schema->seriesElementType,schema):0;
            if(const auto index=universalIntegerLiteral(value->children[1]);index&&(*index<0||static_cast<std::size_t>(*index)>=width)){add("E2540","Array state coordinate lies outside its full real-coordinate layout.",value->span);return Type::Invalid;}return Type::RealSeries;
        }
        if(count==0||!universalArrayType(value->children[0]->type))return {};
        const auto& first=value->children[0];const auto source=first->schema;const auto dimensions=universalExpressionShape(first);const bool generic=first->type==Type::ElementMatrix,fixed=!dimensions.empty();
        if(!generic&&!fixed)return {};
        if(name=="transpose"){if(count!=1){add("E2540","transpose accepts one matrix.",value->span);return Type::Invalid;}auto transposed=dimensions;if(transposed.size()==2)std::swap(transposed[0],transposed[1]);value->schema=universalArraySchema(source,std::move(transposed));return first->type;}
        if(name=="coeff"&&generic){if(count!=2||value->children[1]->type!=Type::Integer){add("E2540","coeff(matrix,index0) requires a zero-based integer coordinate.",value->span);return Type::Invalid;}const auto definition=source?source->algebra:algebra::universal::DefinitionPtr{};if(!definition)return Type::Invalid;if(const auto index=universalIntegerLiteral(value->children[1]);index&&(*index<0||static_cast<std::size_t>(*index)>=definition->dimension())){add("E2540","Matrix coefficient index exceeds its entry algebra dimension.",value->span);return Type::Invalid;}value->schema=universalArraySchema({},dimensions);return Type::RealMatrix;}
        if(name=="size"||name=="numel"||name=="length"){if(name=="size"&&count==1){value->schema=universalArraySchema({}, {1,2});return Type::RealMatrix;}if(count!=(name=="size"?2u:1u)||(name=="size"&&value->children[1]->type!=Type::Integer)){add("E2540","Use size(A[,dimension]), numel(A), length(A).",value->span);return Type::Invalid;}return Type::Integer;}
        const bool elementary=name=="sin"||name=="cos"||name=="exp"||name=="sinh"||name=="cosh";
        if(elementary||name=="conj"||name=="scalar"||name=="pure"||name=="norm2"){
            if(count!=1){add("E2540","Entrywise array intrinsic accepts one array.",value->span);return Type::Invalid;}
            if(generic){if(!source||!source->algebra)return Type::Invalid;const auto& definition=*source->algebra;
                if(elementary){const auto& property=definition.properties().powerAssociative;if(definition.unit().empty()||!property.value||property.evidence!=algebra::universal::Evidence::ProvedExact){add("E2540","Entrywise whole-element calculus requires a unit and exactly proved power associativity.",value->span);return Type::Invalid;}}
                if(name=="conj"&&definition.conjugation().empty()){add("E2540","Entrywise conjugation requires an explicit conjugation map.",value->span);return Type::Invalid;}
                if(name=="norm2"&&definition.metric().empty()){add("E2540","Entrywise norm2 requires an explicit positive metric.",value->span);return Type::Invalid;}
                if((name=="scalar"||name=="pure")&&!universalFirstUnit(definition)){add("E2540","scalar/pure require an exactly first-basis unit.",value->span);return Type::Invalid;}
            }
            value->schema=universalArraySchema(name=="scalar"||name=="norm2"?std::shared_ptr<const ValueSchema>{}:source,dimensions);return name=="scalar"||name=="norm2"?Type::RealMatrix:first->type;
        }
        if(name=="sum"||name=="mean"){
            if(count<1||count>2||(count==2&&value->children[1]->type!=Type::Integer)){add("E2540","Use sum/mean(A[,positive_dimension]).",value->span);return Type::Invalid;}
            auto output=dimensions;if(output.size()==2){const auto axis=count==1?std::optional<long long>(output[0]==1?2:1):universalIntegerLiteral(value->children[1]);if(axis&&*axis>=1&&*axis<=2)output[static_cast<std::size_t>(*axis-1)]=1;else if(!axis)output.clear();}
            value->schema=universalArraySchema(source,std::move(output));return first->type;
        }
        if(name=="norm"){if(count==1||count==2)return Type::Real;}
        return {};
    }
