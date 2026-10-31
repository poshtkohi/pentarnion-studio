    std::shared_ptr<const ValueSchema> tensorSchema(Type entry,std::vector<std::size_t> shape,
        algebra::universal::DefinitionPtr algebra={}) const {
        auto result=std::make_shared<ValueSchema>();result->tensorEntryType=entry;
        result->shape=std::move(shape);result->algebra=std::move(algebra);return result;
    }
    std::vector<std::size_t> tensorLiteralShape(const ExprPtr& value) {
        std::vector<std::size_t> shape;
        if(value->kind!=ExprKind::MatrixLiteral||value->children.empty()||value->children.size()>8){
            add("E2550","Tensor shapes require a literal vector of1..8 positive integer dimensions.",value->span);return shape;
        }
        std::size_t cells=1;
        for(const auto& dimension:value->children){
            const auto literal=universalIntegerLiteral(dimension);
            if(!literal||*literal<1||*literal>1000000||cells>1000000/static_cast<std::size_t>(*literal)){
                add("E2550","A tensor shape must have positive integer extents and at most one million elements.",dimension->span);return {};
            }
            shape.push_back(static_cast<std::size_t>(*literal));cells*=shape.back();
        }
        if(value->rows!=1&&value->columns!=1){add("E2550","A tensor shape is a vector, not a dimension matrix.",value->span);return {};}
        return shape;
    }
    Type tensorDeclaredType() {
        if(!match("<"))return Type::Tensor;
        Type entry=Type::Real;algebra::universal::DefinitionPtr definition;
        if(current().kind!=TokenKind::Integer){
            const auto entryToken=name();const auto entryName=text(entryToken);
            if(entryName=="real")entry=Type::Real;
            else if(entryName=="complex")entry=Type::Complex;
            else if(entryName=="pentarnion")entry=Type::Pentarnion;
            else {const auto found=m_bindings.find(std::string(entryName));
                if(found==m_bindings.end()||found->second.type!=Type::Algebra||!found->second.schema||!found->second.schema->algebra)
                    add("E2550","A tensor entry type requires real, complex, pentarnion or a prior immutable algebra binding.",entryToken.span);
                else {entry=Type::Element;definition=found->second.schema->algebra;}
            }
            expect(",");
        }
        std::vector<std::size_t> shape;std::size_t cells=1;
        do {const auto dimension=advance();const auto count=dimension.kind==TokenKind::Integer?unsignedInteger(text(dimension)):std::optional<std::uint64_t>{};
            if(!count||!*count||*count>1000000||cells>1000000/static_cast<std::size_t>(*count)||shape.size()>=8)
                add("E2550","A fixed tensor type requires1..8 positive literal extents and at most one million elements.",dimension.span);
            else {shape.push_back(static_cast<std::size_t>(*count));cells*=shape.back();}
        }while(match(","));
        expect(">");m_lastDeclaredSchema=tensorSchema(entry,std::move(shape),std::move(definition));return Type::Tensor;
    }
    Statement tensorDeclaration() {
        const auto beginning=current();(void)declaredTypeName();const auto declared=m_lastDeclaredSchema;
        const auto identifier=name();const std::string spelling(text(identifier));
        const bool invalidName=protectedName(identifier)||m_bindings.contains(spelling);
        if(invalidName)add("E1203","A tensor declaration requires a new unprotected identifier.",identifier.span);
        expect("=");auto initial=initializer();const auto actual=check(initial);
        if(actual!=Type::Tensor&&actual!=Type::Invalid)add("E2550","Use an explicit tensor(...) constructor to initialize a tensor.",initial->span);
        if(actual==Type::Tensor&&declared&&!compatibleSchema(declared,initial->schema))add("E2550","Tensor initializer differs from its complete entry algebra or shape.",initial->span);
        const auto schema=declared?declared:initial->schema;
        if(!invalidName){m_bindings.emplace(spelling,Binding{Type::Tensor,identifier.span,{},{},schema});m_result.symbols.push_back({spelling,Type::Tensor,identifier.span,{},{},schema});}
        Statement result;result.kind=StatementKind::Declaration;result.name=spelling;result.nameSpan=identifier.span;
        result.declaredType=Type::Tensor;result.declaredSchema=schema;result.expression=initial;
        result.span=terminate(beginning.span,initial->span);return result;
    }
    std::optional<Type> checkTensorIndex(const ExprPtr& value,Type type) {
        if(type!=Type::Tensor)return {};
        const auto schema=value->children.front()->schema;if(!schema){add("E2550","Tensor indexing requires a resolved entry descriptor.",value->span);return Type::Invalid;}
        const auto indices=value->children.size()-1;bool valid=indices>=1&&indices<=8,slice=false,known=!schema->shape.empty();
        if(known&&indices!=1&&indices!=schema->shape.size()){add("E2550","Use one linear index or exactly one selector per tensor axis.",value->span);valid=false;}
        std::vector<std::size_t> output;
        for(std::size_t axis=0;axis<indices;++axis){
            const auto& index=value->children[axis+1];const auto actual=check(index);std::size_t extent=0;
            if(known){extent=1;if(indices==1)for(const auto n:schema->shape)extent*=n;else if(axis<schema->shape.size())extent=schema->shape[axis];}
            if(actual==Type::Integer){
                output.push_back(1);
                if(const auto literal=universalIntegerLiteral(index);literal&&(*literal<1||(extent&&static_cast<std::size_t>(*literal)>extent))){add("E2550","Tensor index lies outside its one-based shape.",index->span);valid=false;}
            }else if(rangeType(actual)||matrixType(actual)){
                slice=true;std::size_t selected=0;
                if(index->kind==ExprKind::AllIndices)selected=extent;
                else if(index->kind==ExprKind::Range&&(index->children.size()==2||index->children.size()==3)){
                    const auto first=universalIntegerLiteral(index->children.front()),last=universalIntegerLiteral(index->children.back());
                    const auto step=index->children.size()==3?universalIntegerLiteral(index->children[1]):std::optional<long long>(1);
                    if(first&&last&&step){
                        if(*first<1||*last<1||!*step||(extent&&(static_cast<std::size_t>(*first)>extent||static_cast<std::size_t>(*last)>extent))||(*step>0?*last<*first:*last>*first)){add("E2550","Tensor slices must select a nonempty in-bounds sequence.",index->span);valid=false;}
                        else selected=static_cast<std::size_t>((*last-*first)/ *step)+1;
                    }
                }else if(index->kind==ExprKind::MatrixLiteral){selected=index->children.size();for(const auto& item:index->children){const auto n=universalIntegerLiteral(item);if(!n||*n<1||(extent&&static_cast<std::size_t>(*n)>extent)){add("E2550","Tensor index lists require positive in-bounds integer entries.",item->span);valid=false;}}}
                if(!selected)known=false;
                output.push_back(selected);
            }else {add("E2550","Tensor selectors require integer scalars, ranges, index vectors or colon.",index->span);valid=false;}
        }
        if(slice){value->schema=tensorSchema(schema->tensorEntryType,known?std::move(output):std::vector<std::size_t>{},schema->algebra);return valid?Type::Tensor:Type::Invalid;}
        value->schema=schema->tensorEntryType==Type::Element?tensorSchema(Type::Real,{},schema->algebra):std::shared_ptr<const ValueSchema>{};
        return valid?schema->tensorEntryType:Type::Invalid;
    }
    std::optional<Type> checkTensorBinary(const ExprPtr& value,Type left,Type right) {
        if(left!=Type::Tensor&&right!=Type::Tensor)return {};
        const auto& a=value->children[left==Type::Tensor?0:1];const auto schema=a->schema;
        if(!schema){add("E2550","Tensor arithmetic requires a resolved shape and entry type.",value->span);return Type::Invalid;}
        const bool pair=left==Type::Tensor&&right==Type::Tensor;const auto& op=value->text;
        if(pair){
            if(!compatibleSchema(schema,value->children[1]->schema)){add("E2550","Tensor operands must have identical shapes and entry algebras.",value->span);return Type::Invalid;}
            if(op=="=="||op=="!="||op=="~=")return Type::Integer;
            if(op!="+"&&op!="-"&&op!=".*"){add("E2550","Tensor pairs support +, -, entrywise .* and equality; matrix contraction is an explicit separate operation.",value->span);return Type::Invalid;}
        }else{
            const auto other=left==Type::Tensor?right:left;const auto& scalar=value->children[left==Type::Tensor?1:0];
            if(!scalarType(other)&&(other!=schema->tensorEntryType||
                (other==Type::Element&&!compatibleSchema(tensorSchema(Type::Real,{},schema->algebra),scalar->schema)))){
                add("E2550","Tensor scalar operands must be real or belong to the tensor's exact entry algebra.",scalar->span);return Type::Invalid;
            }
            if(op==".^"&&left==Type::Tensor&&other==Type::Integer){value->schema=schema;return Type::Tensor;}
            if(op!="+"&&op!="-"&&op!="*"&&op!=".*"&&!((op=="/"||op=="./")&&left==Type::Tensor&&scalarType(other))){
                add("E2550","Tensor scalar arithmetic supports addition, ordered multiplication and real right division.",value->span);return Type::Invalid;
            }
        }
        value->schema=schema;return Type::Tensor;
    }
    std::optional<Type> checkTensorCall(const ExprPtr& value) {
        const auto& name=value->text;const auto count=value->children.size();
        if(name=="tensor"){
            if(count<2||count>3){add("E2550","Use tensor(values,[literal_shape][,entry_algebra_or_kind]).",value->span);return Type::Invalid;}
            const auto shape=tensorLiteralShape(value->children[1]);if(shape.empty())return Type::Invalid;
            const auto& source=value->children[0];Type entry=source->type==Type::Tensor&&source->schema?source->schema->tensorEntryType:
                source->type==Type::PentarnionMatrix?Type::Pentarnion:source->type==Type::ElementMatrix?Type::Element:
                scalarType(source->type)||matrixType(source->type)||rangeType(source->type)?Type::Real:source->type;
            auto definition=source->schema?source->schema->algebra:algebra::universal::DefinitionPtr{};
            if(count==3){const auto& kind=value->children[2];
                if(kind->type==Type::Algebra&&kind->schema&&kind->schema->algebra){entry=Type::Element;definition=kind->schema->algebra;}
                else if(kind->kind==ExprKind::StringLiteral&&(kind->text=="complex"||kind->text=="pentarnion"||kind->text=="real")){entry=kind->text=="complex"?Type::Complex:kind->text=="pentarnion"?Type::Pentarnion:Type::Real;definition.reset();}
                else {add("E2550","Explicit tensor entries require a literal real/complex/pentarnion kind or immutable algebra binding.",kind->span);return Type::Invalid;}
            }
            if(entry!=Type::Real&&entry!=Type::Pentarnion&&entry!=Type::Complex&&entry!=Type::Element){add("E2550","Tensor values require real, complex or complete algebra coefficients.",source->span);return Type::Invalid;}
            value->schema=tensorSchema(entry,shape,std::move(definition));return Type::Tensor;
        }
        if(!count||value->children.front()->type!=Type::Tensor)return {};
        const auto schema=value->children.front()->schema;if(!schema)return Type::Invalid;
        if(name=="tensorvalues"||name=="tensorcoords"){if(count!=1){add("E2550","Use tensorvalues(T).",value->span);return Type::Invalid;}if(!schema->shape.empty()){std::size_t cells=1;for(const auto extent:schema->shape)cells*=extent;value->schema=universalArraySchema({}, {cells,coordinateWidth(schema->tensorEntryType,schema)});}return Type::RealMatrix;}
        if(name=="tensorinfo"){
            if(count!=1){add("E2550","Use tensorinfo(T).",value->span);return Type::Invalid;}
            auto result=std::make_shared<ValueSchema>();for(const auto field:{"rank","elements","components","bytes"})result->fields.push_back({field,Type::Integer,{}});
            for(const auto field:{"entry_type","algebra_hash","order"})result->fields.push_back({field,Type::String,{}});
            result->fields.push_back({"shape",Type::RealMatrix,{}});result->fields.push_back({"gamma",Type::Real,{}});value->schema=result;return Type::Record;
        }
        if(name=="numel"||name=="length"||name=="size"||name=="ndims"){
            if((name!="size"&&count!=1)||(name=="size"&&(count<1||count>2))){add("E2550","Use size(T[,axis]), numel(T) or length(T).",value->span);return Type::Invalid;}
            if(name=="size"&&count==1){value->schema=universalArraySchema({}, {1,schema->shape.size()});return Type::RealMatrix;}
            if(count==2&&value->children[1]->type!=Type::Integer){add("E2550","Tensor size axis requires an integer scalar.",value->span);return Type::Invalid;}return Type::Integer;
        }
        if(name=="sum"||name=="mean"){
            if(count!=1&&(count!=2||value->children[1]->kind!=ExprKind::StringLiteral||value->children[1]->text!="all")){add("E2550","Tensor reductions currently sum all entries; use sum/mean(T[,\"all\"]).",value->span);return Type::Invalid;}
            value->schema=schema->tensorEntryType==Type::Element?tensorSchema(Type::Real,{},schema->algebra):std::shared_ptr<const ValueSchema>{};return schema->tensorEntryType;
        }
        if(name=="norm"){if(count>2||(count==2&&(value->children[1]->kind!=ExprKind::StringLiteral||value->children[1]->text!="fro"))){add("E2550","Use norm(T[,\"fro\"]).",value->span);return Type::Invalid;}return Type::Real;}
        if(name=="coeff"){
            if(count!=2||value->children[1]->type!=Type::Integer){add("E2550","Use coeff(T,zero_based_coefficient).",value->span);return Type::Invalid;}
            const auto width=coordinateWidth(schema->tensorEntryType,schema);
            if(const auto index=universalIntegerLiteral(value->children[1]);index&&(*index<0||static_cast<std::size_t>(*index)>=width)){add("E2550","Tensor coefficient is outside the complete entry basis.",value->span);return Type::Invalid;}
            value->schema=tensorSchema(Type::Real,schema->shape);return Type::Tensor;
        }
        if(name=="sin"||name=="cos"||name=="exp"||name=="sinh"||name=="cosh"||name=="conj"){
            if(count!=1){add("E2550","Entrywise tensor functions accept one tensor.",value->span);return Type::Invalid;}
            if(schema->tensorEntryType==Type::Element){const auto definition=schema->algebra;
                if(!definition){add("E2550","Tensor entries lack an immutable algebra definition.",value->span);return Type::Invalid;}
                if(name=="conj"?definition->conjugation().empty():definition->unit().empty()||!definition->properties().powerAssociative.value||definition->properties().powerAssociative.evidence!=algebra::universal::Evidence::ProvedExact){
                    add("E2550","The tensor entry algebra does not certify this whole-element function.",value->span);return Type::Invalid;
                }
            }
            value->schema=schema;return Type::Tensor;
        }
        return {};
    }
