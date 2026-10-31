    using TensorData=detail::TensorData;
    using TensorLimits=detail::TensorLimits;
    using TensorError=detail::TensorError;
// First-class tensors preserve explicit rank, entry kind and coefficient order.
// Existing row-major matrices keep their separate ABI and semantics.
    TensorLimits tensorLimits()const{return {std::min<std::size_t>(1000000,options_.maxMatrixElements),std::min<std::size_t>(8000000,options_.maxMatrixCoordinates),1073741824};}
    std::size_t checkedTensorCount(const std::vector<std::size_t>& shape,std::size_t width,SourceSpan span)const{
        try{const auto limits=tensorLimits();const auto count=detail::tensorElements(shape,limits.elements);if(!width||width>64||count>limits.coordinates/width)fail("R1005","Tensor full-coefficient packing exceeds its coordinate allowance.",span);return count;}
        catch(const TensorError& error){fail(error.status==7?"R1005":"R2550",error.what(),span);}
    }
    TensorValue tensorValue(Type kind,UniversalDefinition definition,double gamma,std::vector<std::size_t> shape,std::vector<double> values,SourceSpan span){
        try{return detail::makeTensor(kind,std::move(definition),gamma,std::move(shape),std::move(values),tensorLimits(),options_.cancellation.get());}
        catch(const TensorError& error){fail(error.status==3?"R1006":error.status==7?"R1005":"R2550",error.what(),span);}
    }
    Value tensorEntry(const TensorData& source,std::size_t cell,SourceSpan span)const{
        const auto width=source.coefficientWidth();if(cell>=source.elements())fail("R2550","Tensor index exceeds its checked shape.",span);const auto at=cell*width;
        if(source.entryType==Type::Real)return source.values[at];
        if(source.entryType==Type::Complex)return std::complex<double>(source.values[at],source.values[at+1]);
        if(source.entryType==Type::Pentarnion){algebra::Coefficients c{};std::copy_n(source.values.begin()+at,5,c.begin());return P(algebra::Context(source.gamma),c);}
        return UniversalElement(source.algebra,std::vector<double>(source.values.begin()+at,source.values.begin()+at+width));
    }
    std::vector<double> tensorEntryCoordinates(const Value& value,Type kind,const UniversalDefinition& definition,double gamma,SourceSpan span){
        if(kind==Type::Element){try{if(const auto* item=std::get_if<UniversalElement>(&value)){if(!definition||item->definition()->hash()!=definition->hash())fail("R2550","Tensor entries cannot change their immutable algebra.",span);return item->coefficients();}const auto scalar=real(value,span);if(scalar==0&&definition&&definition->unit().empty())return std::vector<double>(definition->dimension(),0);return UniversalElement::scalar(definition,scalar).coefficients();}catch(const algebra::universal::Error& error){fail(error.code,error.what(),span);}}
        if(kind==Type::Complex){if(const auto* number=std::get_if<std::complex<double>>(&value))return {finite(number->real(),span),finite(number->imag(),span)};return {real(value,span),0};}
        if(kind==Type::Pentarnion){if(const auto* item=std::get_if<P>(&value)){if(item->context().gamma()!=gamma)fail("R2550","Tensor entry uses a different Pentarnion parameter.",span);return {item->coefficients().begin(),item->coefficients().end()};}return {real(value,span),0,0,0,0};}
        return {real(value,span)};
    }
    std::vector<std::size_t> tensorShape(const Value& value,SourceSpan span){
        const auto raw=numericVector(value,span,false);if(raw.empty()||raw.size()>8)fail("R2550","tensor dimensions must be a positive rank1..8 vector.",span);std::vector<std::size_t> shape;
        for(const auto extent:raw){if(extent<1||extent>1000000||extent!=std::floor(extent))fail("R2550","Tensor dimensions require positive bounded integers.",span);shape.push_back(static_cast<std::size_t>(extent));}return shape;
    }
    Value constructTensor(const std::vector<Value>& arguments,SourceSpan span){
        if(arguments.size()<2||arguments.size()>3)fail("R2550","Use tensor(values,[dimensions][,entry_kind_or_algebra]).",span);
        auto shape=tensorShape(arguments[1],span);Type kind=Type::Real;UniversalDefinition definition;bool coordinates=false;
        if(arguments.size()==3){
            if(const auto* algebra=std::get_if<UniversalDefinition>(&arguments[2])){definition=*algebra;kind=Type::Element;coordinates=std::holds_alternative<MatrixValue>(arguments[0]);}
            else if(const auto* name=std::get_if<std::string>(&arguments[2])){if(*name=="complex"){kind=Type::Complex;coordinates=std::holds_alternative<MatrixValue>(arguments[0]);}else if(*name=="pentarnion"){kind=Type::Pentarnion;coordinates=std::holds_alternative<MatrixValue>(arguments[0]);}else if(*name=="real"){kind=Type::Real;coordinates=std::holds_alternative<MatrixValue>(arguments[0]);}else fail("R2550","Tensor entry kinds are real, complex, pentarnion or an immutable algebra.",span);}
            else fail("R2550","Tensor entry kind must be a checked immutable algebra or explicit string.",span);
        }else if(const auto* source=std::get_if<TensorValue>(&arguments[0])){kind=(*source)->entryType;definition=(*source)->algebra;}
        else if(const auto* source=std::get_if<UniversalMatrix>(&arguments[0])){kind=Type::Element;definition=(*source)->algebra;}
        else if(const auto* source=std::get_if<UniversalElement>(&arguments[0])){kind=Type::Element;definition=source->definition();}
        else if(std::holds_alternative<std::complex<double>>(arguments[0]))kind=Type::Complex;
        else if(std::holds_alternative<P>(arguments[0])||(std::holds_alternative<MatrixValue>(arguments[0])&&std::get<MatrixValue>(arguments[0])->pentarnion))kind=Type::Pentarnion;
        const auto width=kind==Type::Element?definition->dimension():kind==Type::Pentarnion?5u:kind==Type::Complex?2u:1u;const auto count=checkedTensorCount(shape,width,span);std::vector<double> values(count*width);const auto gamma=context_.gamma();
        auto place=[&](const Value& entry,std::size_t n){tick(span);const auto coefficients=tensorEntryCoordinates(entry,kind,definition,gamma,span);std::copy(coefficients.begin(),coefficients.end(),values.begin()+n*width);};
        if(const auto* source=std::get_if<TensorValue>(&arguments[0])){
            TensorData expected;expected.entryType=kind;expected.algebra=definition;expected.gamma=gamma;expected.shape=(*source)->shape;
            if(!detail::sameTensorDescriptor(expected,**source)||(*source)->elements()!=count)fail("R2550","Explicit tensor reshape preserves entry kind, algebra and entry count.",span);
            values=(*source)->values;
        }else if(coordinates){
            const auto input=numericArray(arguments[0],span);if(input.components!=1||input.columns!=width||(input.rows!=count&&input.rows!=1))fail("R2550","Explicit tensor coordinates must be N-by-entry-width real rows, N=entries or1.",span);
            for(std::size_t n=0;n<count;++n){tick(span);std::copy_n(input.values.begin()+(input.rows==1?0:n)*width,width,values.begin()+n*width);}
        }else if(const auto* source=std::get_if<UniversalMatrix>(&arguments[0])){
            const auto entries=(*source)->rows*(*source)->columns;if(entries!=count&&entries!=1)fail("R2550","Tensor shape must preserve the input matrix entry count or broadcast one entry.",span);
            for(std::size_t n=0;n<count;++n){const auto cell=entries==1?0:n;place(universalMatrixElement(**source,cell%(*source)->rows,cell/(*source)->rows,span),n);}
        }else if(std::holds_alternative<MatrixValue>(arguments[0])||rangeValue(arguments[0])){
            const auto matrix=rangeMatrix(arguments[0],span);const auto& source=*std::get<MatrixValue>(matrix);if(source.values.size()!=count&&source.values.size()!=1)fail("R2550","Tensor shape must preserve the input matrix entry count or broadcast one entry.",span);
            for(std::size_t n=0;n<count;++n){const auto cell=source.values.size()==1?0:(n%source.rows)*source.columns+n/source.rows;place(source.pentarnion?Value(source.values[cell]):Value(source.values[cell].coefficients()[0]),n);}
        }else{const auto coefficients=tensorEntryCoordinates(arguments[0],kind,definition,gamma,span);for(std::size_t n=0;n<count;++n){tick(span);std::copy(coefficients.begin(),coefficients.end(),values.begin()+n*width);}}
        return tensorValue(kind,definition,gamma,std::move(shape),std::move(values),span);
    }
    Value convertTensor(const Value& source,const std::shared_ptr<const language::ValueSchema>& schema,SourceSpan span){
        const auto* tensor=std::get_if<TensorValue>(&source);if(!tensor||!*tensor||!schema||(!schema->shape.empty()&&schema->shape!=(*tensor)->shape)||schema->tensorEntryType!=(*tensor)->entryType)fail("R2550","A tensor assignment/parameter requires its exact declared rank, shape and entry type.",span);
        if((*tensor)->entryType==Type::Element&&(!schema->algebra||schema->algebra->hash()!=(*tensor)->algebra->hash()))fail("R2550","Tensor assignment cannot change its immutable algebra.",span);
        if((*tensor)->entryType==Type::Pentarnion&&(*tensor)->gamma!=context_.gamma())fail("R2550","Tensor assignment cannot change the workspace Pentarnion parameter.",span);
        return *tensor;
    }
    struct TensorSelection{std::vector<std::size_t> positions,shape;bool scalar=true;};
    TensorSelection tensorSelection(const TensorData& source,const std::vector<language::ExprPtr>& selectors,SourceSpan span,std::size_t depth){
        if(selectors.empty()||(selectors.size()!=1&&selectors.size()!=source.shape.size()))fail("R2550","Use one linear tensor selector or one selector per dimension.",span);
        TensorSelection result;
        if(selectors.size()==1){const auto selection=indexSelection(selectors[0],source.elements(),depth);result.positions=selection.positions;result.scalar=selection.scalar;result.shape={result.positions.size()};}
        else{
            std::vector<IndexSelection> selections;std::size_t count=1;for(std::size_t axis=0;axis<selectors.size();++axis){selections.push_back(indexSelection(selectors[axis],source.shape[axis],depth));const auto size=selections.back().positions.size();if(!size)fail("R2550","Empty tensor slices are outside the positive-shape native tensor contract.",span);if(count>tensorLimits().elements/size)fail("R1005","Tensor slice exceeds its entry allowance.",span);count*=size;result.shape.push_back(size);result.scalar=result.scalar&&selections.back().scalar;}
            result.positions.reserve(count);for(std::size_t selected=0;selected<count;++selected){auto q=selected;std::size_t sourceIndex=0,stride=1;for(std::size_t axis=0;axis<selections.size();++axis){const auto size=selections[axis].positions.size();sourceIndex+=selections[axis].positions[q%size]*stride;q/=size;stride*=source.shape[axis];}result.positions.push_back(sourceIndex);}
        }
        if(result.positions.empty())fail("R2550","Empty tensor slices are outside the positive-shape native tensor contract.",span);
        return result;
    }
    std::optional<Value> tensorReadIndex(const Value& base,const std::vector<language::ExprPtr>& selectors,SourceSpan span,std::size_t depth){
        const auto* tensor=std::get_if<TensorValue>(&base);if(!tensor)return {};const auto selection=tensorSelection(**tensor,selectors,span,depth);if(selection.scalar)return tensorEntry(**tensor,selection.positions.front(),span);
        const auto width=(*tensor)->coefficientWidth();checkedTensorCount(selection.shape,width,span);std::vector<double> values(selection.positions.size()*width);for(std::size_t n=0;n<selection.positions.size();++n){tick(span);std::copy_n((*tensor)->values.begin()+selection.positions[n]*width,width,values.begin()+n*width);}return Value(tensorValue((*tensor)->entryType,(*tensor)->algebra,(*tensor)->gamma,selection.shape,std::move(values),span));
    }
    Value tensorWriteIndex(const TensorValue& source,const std::vector<language::ExprPtr>& selectors,const Expr& replacement,SourceSpan span,std::size_t depth){
        const auto selection=tensorSelection(*source,selectors,span,depth);const auto right=evaluate(replacement,depth+1);const auto width=source->coefficientWidth();auto values=source->values;
        if(const auto* tensor=std::get_if<TensorValue>(&right)){
            TensorData expected;expected.entryType=source->entryType;expected.algebra=source->algebra;expected.gamma=source->gamma;expected.shape=(*tensor)->shape;if(!detail::sameTensorDescriptor(expected,**tensor)||(*tensor)->elements()!=selection.positions.size())fail("R2550","Tensor slice replacement must preserve entry kind/algebra and selected count.",span);
            for(std::size_t n=0;n<selection.positions.size();++n){tick(span);std::copy_n((*tensor)->values.begin()+n*width,width,values.begin()+selection.positions[n]*width);}
        }else{const auto coefficients=tensorEntryCoordinates(right,source->entryType,source->algebra,source->gamma,span);for(const auto target:selection.positions){tick(span);std::copy(coefficients.begin(),coefficients.end(),values.begin()+target*width);}}
        return tensorValue(source->entryType,source->algebra,source->gamma,source->shape,std::move(values),span);
    }
    std::optional<Value> tensorBinary(const Expr& expression,const Value& left,const Value& right){
        const auto* a=std::get_if<TensorValue>(&left);const auto* b=std::get_if<TensorValue>(&right);if(!a&&!b)return {};const auto span=expression.span;const auto& source=**(a?a:b);const auto& op=expression.text;
        if(a&&b&&!detail::sameTensorDescriptor(**a,**b))fail("R2550","Tensor operations require equal rank, shape, entry kind and immutable algebra.",span);
        if(op=="=="||op=="!="||op=="~="){const bool same=a&&b&&detail::sameTensorDescriptor(**a,**b)&&(*a)->values==(*b)->values;return Value(I((op=="==")==same));}
        if(op=="*"&&a&&b)fail("R2550","Tensor multiplication is entrywise with .*; matrix contraction requires an explicit separate operation.",span);
        if(op!="+"&&op!="-"&&op!="*"&&op!=".*"&&op!="/"&&op!="./"&&op!=".^")fail("R2550","Unsupported tensor operator. Use +,-,.*,real division or entrywise integer powers.",span);
        if((op=="/"||op=="./")&&((b&&(*b)->entryType!=Type::Real)||(!b&&!std::holds_alternative<I>(right)&&!std::holds_alternative<double>(right))))fail("R2550","Tensor division currently accepts real denominators only.",span);
        if(op==".^"&&b)fail("R2550","Tensor entrywise powers currently require one scalar exponent.",span);
        // Scalar multiplication preserves a nonunital algebra; addition/subtraction
        // still require the algebra's verified scalar embedding in binary().
        Expr scalar=expression;if(op.size()==2&&op.front()=='.')scalar.text=op.substr(1);std::vector<double> values(source.values.size());const auto width=source.coefficientWidth();
        for(std::size_t n=0;n<source.elements();++n){tick(span);const Value x=a?tensorEntry(**a,n,span):left,y=b?tensorEntry(**b,n,span):right;const auto value=binary(scalar,x,y);const auto coordinates=tensorEntryCoordinates(value,source.entryType,source.algebra,source.gamma,span);std::copy(coordinates.begin(),coordinates.end(),values.begin()+n*width);}
        return Value(tensorValue(source.entryType,source.algebra,source.gamma,source.shape,std::move(values),span));
    }
    std::optional<Value> tensorCall(const Expr& expression,const std::vector<Value>& arguments){
        const auto& name=expression.text;const auto span=expression.span;if(name=="tensor")return constructTensor(arguments,span);if(arguments.empty()||!std::holds_alternative<TensorValue>(arguments[0]))return {};const auto& source=*std::get<TensorValue>(arguments[0]);
        if(name=="tensorinfo"){if(arguments.size()!=1)fail("R2550","Use tensorinfo(tensor).",span);return makeRecord({{"rank",I(source.shape.size())},{"elements",I(source.elements())},{"components",I(source.coefficientWidth())},{"bytes",I(source.retainedBytes())},{"entry_type",std::string(language::typeName(source.entryType))},{"algebra_hash",source.algebra?source.algebra->hash():std::string()},{"order",std::string("column_major_entries_full_coefficients")},{"shape",numericResult(std::vector<double>(source.shape.begin(),source.shape.end()),1,source.shape.size(),1,span)},{"gamma",source.entryType==Type::Pentarnion?source.gamma:0.0}},span);}
        if(name=="norm"){if(arguments.size()>2||(arguments.size()==2&&(!std::holds_alternative<std::string>(arguments[1])||std::get<std::string>(arguments[1])!="fro")))fail("R2550","Use norm(tensor[,\"fro\"]); the norm includes every real coefficient.",span);long double norm=0;for(const auto value:source.values){tick(span);norm=std::hypot(norm,static_cast<long double>(value));}return Value(finite(static_cast<double>(norm),span));}
        if(name=="size"){if(arguments.size()==1)return numericResult(std::vector<double>(source.shape.begin(),source.shape.end()),1,source.shape.size(),1,span);if(arguments.size()!=2)fail("R2550","Use size(tensor[,dimension]).",span);const auto axis=integer(arguments[1],span);if(axis<1)fail("R2550","Tensor size dimension must be positive.",span);return Value(I(static_cast<std::size_t>(axis)<=source.shape.size()?source.shape[axis-1]:1));}
        if(name=="numel"||name=="length"||name=="ndims"){if(arguments.size()!=1)fail("R2550","Tensor shape queries take one argument.",span);return Value(I(name=="numel"?source.elements():name=="ndims"?source.shape.size():*std::max_element(source.shape.begin(),source.shape.end())));}
        if(name=="tensorcoords"||name=="tensorvalues"){if(arguments.size()!=1)fail("R2550","Use tensorcoords(tensor) for full entry coefficient rows.",span);return numericResult(source.values,source.elements(),source.coefficientWidth(),1,span);}
        if(name=="coeff"){if(arguments.size()!=2)fail("R2550","Use coeff(tensor,zero_based_component).",span);const auto c=integer(arguments[1],span);const auto width=source.coefficientWidth();if(c<0||static_cast<std::size_t>(c)>=width)fail("R2550","Tensor coefficient exceeds the explicit entry width.",span);std::vector<double> values(source.elements());for(std::size_t n=0;n<values.size();++n){tick(span);values[n]=source.values[n*width+static_cast<std::size_t>(c)];}return Value(tensorValue(Type::Real,{},context_.gamma(),source.shape,std::move(values),span));}
        if(name=="sum"||name=="mean"){if(arguments.size()>2||(arguments.size()==2&&(!std::holds_alternative<std::string>(arguments[1])||std::get<std::string>(arguments[1])!="all")))fail("R2550","Tensor sum/mean reduce all entries; use an optional literal \"all\".",span);
            const auto width=source.coefficientWidth(),count=source.elements();std::vector<long double> totals(width,0);
            for(std::size_t n=0;n<count;++n){if(n){tick(span);if(source.entryType==Type::Element)universalCharge(source.algebra,span);}for(std::size_t c=0;c<width;++c)totals[c]+=source.values[n*width+c];}
            if(name=="mean"&&source.entryType==Type::Element)universalCharge(source.algebra,span);
            std::vector<double> values(width);for(std::size_t c=0;c<width;++c){const auto wide=name=="mean"?totals[c]/static_cast<long double>(count):totals[c];
                if(!std::isfinite(wide)||std::abs(wide)>std::numeric_limits<double>::max()){if(source.entryType==Type::Element)throw algebra::universal::Error("A4002","Algebra reduction produced a nonfinite/unrepresentable coefficient.");if(source.entryType==Type::Pentarnion)throw algebra::AlgebraError("R3001","The algebra reduction overflows finite binary64 coefficients.");}
                values[c]=finite(static_cast<double>(wide),span);
                if(wide!=0&&values[c]==0){if(source.entryType==Type::Element)throw algebra::universal::Error("A4002","A nonzero algebra reduction coefficient underflowed binary64.");if(source.entryType==Type::Pentarnion)fail("R1001","A nonzero coefficient reduction result underflows binary64.",span);}
            }
            if(source.entryType==Type::Real)return Value(values[0]);
            if(source.entryType==Type::Complex)return Value(std::complex<double>(values[0],values[1]));
            if(source.entryType==Type::Pentarnion){algebra::Coefficients coefficients{};std::copy_n(values.begin(),5,coefficients.begin());return Value(P(algebra::Context(source.gamma),coefficients));}
            return Value(UniversalElement(source.algebra,std::move(values)));
        }
        if(name=="conj"||name=="exp"||name=="sin"||name=="cos"||name=="sinh"||name=="cosh"){if(arguments.size()!=1)fail("R2550","Tensor elementwise functions take one tensor.",span);std::vector<double> values(source.values.size());for(std::size_t n=0;n<source.elements();++n){tick(span);const auto result=call(expression,{tensorEntry(source,n,span)});const auto coordinates=tensorEntryCoordinates(result,source.entryType,source.algebra,source.gamma,span);std::copy(coordinates.begin(),coordinates.end(),values.begin()+n*source.coefficientWidth());}return Value(tensorValue(source.entryType,source.algebra,source.gamma,source.shape,std::move(values),span));}
        return {};
    }
