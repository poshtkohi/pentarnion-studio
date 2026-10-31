    struct UniversalArrayInput {UniversalDefinition algebra;std::size_t rows=0,columns=0;std::vector<double> values;bool scalar=false;};
    std::shared_ptr<UniversalMatrixData> newUniversalMatrix(const UniversalDefinition& algebra,std::size_t rows,std::size_t columns,SourceSpan span) {
        if(!algebra||!algebra->dimension())fail("R2540","A native matrix requires an immutable entry algebra.",span);
        const auto width=algebra->dimension(),elements=std::min<std::size_t>(1000000,options_.maxMatrixElements),coordinates=std::min<std::size_t>(8000000,options_.maxMatrixCoordinates);
        if(rows>100000||columns>100000||(columns&&rows>elements/columns)||(columns&&rows>coordinates/columns/width))fail("R1902","Native matrix shape exceeds its element/coordinate allowance.",span);
        auto result=std::make_shared<UniversalMatrixData>();result->algebra=algebra;result->rows=rows;result->columns=columns;result->values.assign(rows*columns*width,0);return result;
    }
    UniversalElement universalMatrixElement(const UniversalMatrixData& matrix,std::size_t row,std::size_t column,SourceSpan span) const {
        const auto width=matrix.algebra->dimension();if(row>=matrix.rows||column>=matrix.columns)fail("R1901","Native matrix index exceeds its shape.",span);
        const auto offset=(row*matrix.columns+column)*width;return UniversalElement(matrix.algebra,std::vector<double>(matrix.values.begin()+offset,matrix.values.begin()+offset+width));
    }
    UniversalArrayInput universalArrayInput(const Value& value,const UniversalDefinition& algebra,SourceSpan span) {
        UniversalArrayInput result;result.algebra=algebra;
        if(const auto* matrix=std::get_if<UniversalMatrix>(&value)){if(!(*matrix)||(*matrix)->algebra->hash()!=algebra->hash())fail("R2540","Native arrays cannot mix different immutable entry algebras.",span);
        result.rows=(*matrix)->rows;result.columns=(*matrix)->columns;result.values=(*matrix)->values;return result;}
        if(const auto* matrix=std::get_if<MatrixValue>(&value)){
            if((*matrix)->pentarnion)fail("R2540","Legacy Pentarnion arrays cannot mix with a native entry algebra.",span);
        result.rows=(*matrix)->rows;result.columns=(*matrix)->columns;
            const auto width=algebra->dimension();if(result.columns&&result.rows>std::min<std::size_t>(8000000,options_.maxMatrixCoordinates)/result.columns/width)fail("R1902","Embedded real matrix exceeds the native coordinate allowance.",span);
            result.values.reserve(result.rows*result.columns*width);for(const auto& entry:(*matrix)->values){tick(span);const auto scalar=UniversalElement::scalar(algebra,entry.coefficients()[0]);result.values.insert(result.values.end(),scalar.coefficients().begin(),scalar.coefficients().end());}return result;
        }
        result.rows=result.columns=1;result.scalar=true;const auto scalar=std::holds_alternative<UniversalElement>(value)?universalElementValue(value,span):UniversalElement::scalar(algebra,real(value,span));
        if(scalar.definition()->hash()!=algebra->hash())fail("R2540","Native scalar/matrix entry algebras differ.",span);
        result.values=scalar.coefficients();return result;
    }
    void universalArrayDispatch(std::size_t count,std::size_t cost,SourceSpan span,const std::function<void(std::size_t,std::size_t)>& body) {
        const auto per=std::max<std::size_t>(1,cost);if(count&&per>options_.maxNativeOperations/count)fail("R1005","Native matrix work exceeds its allowance.",span);
        const auto work=count*per;chargeNumericWork(std::max<std::size_t>(1,count+work/256),span);
        struct Task {const std::function<void(std::size_t,std::size_t)>& body;static int run(void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity)noexcept{try{static_cast<Task*>(raw)->body(static_cast<std::size_t>(begin),static_cast<std::size_t>(end));return PENTA_NUMERIC_OK;}catch(const std::exception& failure){if(error&&capacity){const std::string text=failure.what();const auto size=std::min<std::size_t>(capacity-1,text.size());std::copy_n(text.data(),size,error);error[size]=0;}return PENTA_NUMERIC_DOMAIN;}catch(...){return PENTA_NUMERIC_INTERNAL;}}} task{body};
        std::string error;const auto status=ThreadBudget::instance().run(count,per,Task::run,&task,parallel_.settings,options_.cancellation,parallel_.statistics,error);
        if(status!=PENTA_NUMERIC_OK)fail(status==PENTA_NUMERIC_CANCELLED?"R1006":"R2540",error.empty()?"Native matrix operation failed.":error,span);
    }
    Value universalArrayLiteral(const Expr& expression,std::size_t depth) {
        const auto algebra=expression.schema?expression.schema->algebra:UniversalDefinition{};if(!algebra)fail("R2540","A native matrix literal has no checked entry definition.",expression.span);
        std::vector<UniversalArrayInput> cells;for(const auto& child:expression.children){tick(child->span);const auto value=rangeMatrix(evaluate(*child,depth+1),child->span);cells.push_back(universalArrayInput(value,algebra,child->span));}
        if(cells.empty())return UniversalMatrix(newUniversalMatrix(algebra,0,0,expression.span));
        auto widths=expression.rowWidths;if(widths.empty())widths.assign(expression.rows,expression.columns);
        struct Block {std::size_t offset,count,height,width;};std::vector<Block> blocks;std::size_t offset=0,rows=0,columns=0;
        for(const auto count:widths){if(!count||count>cells.size()-offset)fail("R1901","Malformed native matrix block row.",expression.span);
        const auto height=cells[offset].rows;std::size_t width=0;
            for(std::size_t n=0;n<count;++n){if(cells[offset+n].rows!=height)fail("R1901","Native matrix blocks must share row heights.",expression.span);width+=cells[offset+n].columns;}
            if(!blocks.empty()&&columns!=width)fail("R1901","Native matrix block rows must have equal widths.",expression.span);
        columns=width;rows+=height;blocks.push_back({offset,count,height,width});offset+=count;
        }
        if(offset!=cells.size())fail("R1901","Native matrix block count is inconsistent.",expression.span);
        auto result=newUniversalMatrix(algebra,rows,columns,expression.span);const auto width=algebra->dimension();std::size_t target=0;
        for(const auto& block:blocks)for(std::size_t row=0;row<block.height;++row)for(std::size_t n=0;n<block.count;++n){const auto& cell=cells[block.offset+n];const auto begin=cell.values.begin()+row*cell.columns*width;const auto count=cell.columns*width;std::copy_n(begin,count,result->values.begin()+target);target+=count;}
        return UniversalMatrix(result);
    }
    std::optional<Value> universalArrayBinary(const Expr& expression,const Value& left,const Value& right) {
        const auto* leftMatrix=std::get_if<UniversalMatrix>(&left);const auto* rightMatrix=std::get_if<UniversalMatrix>(&right);
        if(!leftMatrix&&!rightMatrix&&!(expression.type==Type::ElementMatrix&&expression.schema&&expression.schema->algebra))return {};
        const auto algebra=leftMatrix?(*leftMatrix)->algebra:rightMatrix?(*rightMatrix)->algebra:expression.schema->algebra;const auto span=expression.span;const auto width=algebra->dimension();const auto& op=expression.text;
        if(op=="^"||op==".^"){
            if(!leftMatrix)fail("R2540","Array powers require an array base.",span);
        const auto exponent=integer(right,span);if(exponent<0||exponent>64)fail("R2540","Native matrix powers are bounded to integer exponents0..64.",span);
            if(op==".^"){auto result=newUniversalMatrix(algebra,(*leftMatrix)->rows,(*leftMatrix)->columns,span);universalArrayDispatch(result->rows*result->columns,(width+algebra->terms().size())*std::max<I>(1,exponent),span,[&](std::size_t begin,std::size_t end){for(auto n=begin;n<end;++n){const auto value=algebra::universal::power(universalMatrixElement(**leftMatrix,n/result->columns,n%result->columns,span),exponent);std::copy(value.coefficients().begin(),value.coefficients().end(),result->values.begin()+n*width);}});return Value(UniversalMatrix(result));}
            if((*leftMatrix)->rows!=(*leftMatrix)->columns)fail("R2540","A native matrix power requires a square shape.",span);
            if(exponent==0){auto identity=newUniversalMatrix(algebra,(*leftMatrix)->rows,(*leftMatrix)->columns,span);const auto one=UniversalElement::scalar(algebra,1);for(std::size_t n=0;n<identity->rows;++n)std::copy(one.coefficients().begin(),one.coefficients().end(),identity->values.begin()+(n*identity->columns+n)*width);return Value(UniversalMatrix(identity));}
            Value result=left;Expr product=expression;product.text="*";for(I n=1;n<exponent;++n)result=*universalArrayBinary(product,result,left);return result;
        }
        const auto realScalar=[](const Value& value){return std::holds_alternative<I>(value)||std::holds_alternative<double>(value);};
        if((leftMatrix&&realScalar(right)&&(op=="*"||op==".*"||op=="/"||op=="./"))||(rightMatrix&&realScalar(left)&&(op=="*"||op==".*"))){
            const auto& source=leftMatrix?**leftMatrix:**rightMatrix;const double factor=real(leftMatrix?right:left,span);const bool divide=op=="/"||op=="./";if(divide&&factor==0)fail("R1001","Division by zero.",span);
            auto result=newUniversalMatrix(algebra,source.rows,source.columns,span);universalArrayDispatch(source.rows*source.columns,width,span,[&](std::size_t begin,std::size_t end){for(auto n=begin;n<end;++n){const auto entry=universalMatrixElement(source,n/source.columns,n%source.columns,span);const auto scaled=divide?algebra::universal::divideByReal(entry,factor):algebra::universal::scale(entry,factor);std::copy(scaled.coefficients().begin(),scaled.coefficients().end(),result->values.begin()+n*width);}});return Value(UniversalMatrix(result));
        }
        const bool divide=op=="/"||op=="./";
        const auto a=universalArrayInput(left,algebra,span);UniversalArrayInput b;
        if(divide&&std::holds_alternative<MatrixValue>(right)){const auto& realMatrix=*std::get<MatrixValue>(right);if(realMatrix.pentarnion)fail("R2540","Entrywise divisors must be real.",span);b.algebra=algebra;b.rows=realMatrix.rows;b.columns=realMatrix.columns;}
        else b=universalArrayInput(right,algebra,span);
        if(op=="=="||op=="!="||op=="~="){const bool equal=a.rows==b.rows&&a.columns==b.columns&&a.values==b.values;return Value(I((op=="==")==equal));}
        const bool product=op=="*"&&!a.scalar&&!b.scalar;
        if(product&&a.columns!=b.rows)fail("R1901","Native matrix product dimensions do not contract.",span);
        if(!product&&!a.scalar&&!b.scalar&&(a.rows!=b.rows||a.columns!=b.columns))fail("R1901","Entrywise native matrix operations require equal shapes.",span);
        const auto rows=product?a.rows:a.scalar?b.rows:a.rows,columns=product?b.columns:a.scalar?b.columns:a.columns;auto result=newUniversalMatrix(algebra,rows,columns,span);
        const auto terms=algebra->terms().size()+width;const auto inner=product?a.columns:1;if(inner&&terms>options_.maxNativeOperations/inner)fail("R1005","Native matrix work exceeds its allowance.",span);
        if(divide&&!std::holds_alternative<I>(right)&&!std::holds_alternative<double>(right)&&!std::holds_alternative<MatrixValue>(right))fail("R2540","Native array division denominators must be real.",span);
        universalArrayDispatch(rows*columns,terms*inner,span,[&](std::size_t begin,std::size_t end){for(auto index=begin;index<end;++index){
            std::vector<double> coefficients(width,0);auto entry=[&](const UniversalArrayInput& input,std::size_t cell){const auto at=(input.scalar?0:cell)*width;return UniversalElement(algebra,std::vector<double>(input.values.begin()+at,input.values.begin()+at+width));};
            if(product){auto sum=UniversalElement(algebra,coefficients);for(std::size_t k=0;k<a.columns;++k)sum=algebra::universal::add(sum,algebra::universal::multiply(entry(a,(index/columns)*a.columns+k),entry(b,k*b.columns+index%columns)));coefficients=sum.coefficients();}
            else{const auto x=entry(a,index),y=divide?UniversalElement(algebra,std::vector<double>(width,0)):entry(b,index);UniversalElement value=x;
                if(op=="+")value=algebra::universal::add(x,y);else if(op=="-")value=algebra::universal::subtract(x,y);else if(op=="*"||op==".*")value=algebra::universal::multiply(x,y);
                else if(divide){double denominator=0;if(const auto* realMatrix=std::get_if<MatrixValue>(&right))denominator=(*realMatrix)->values.at(index).coefficients()[0];else denominator=std::holds_alternative<I>(right)?static_cast<double>(std::get<I>(right)):std::get<double>(right);if(denominator==0)throw std::domain_error("Division by a zero real matrix entry.");value=algebra::universal::divideByReal(x,denominator);}
                else throw std::domain_error("Unsupported native matrix operator.");
                coefficients=value.coefficients();}
            std::copy(coefficients.begin(),coefficients.end(),result->values.begin()+index*width);
        }});return Value(UniversalMatrix(result));
    }
    std::optional<Value> universalArrayReadIndex(const Value& base,const std::vector<language::ExprPtr>& selectors,SourceSpan span,std::size_t depth) {
        if(const auto* series=std::get_if<ArraySeries>(&base)){
            if(selectors.size()!=1)fail("R2540","An array series uses one scalar sample selector.",span);
        const auto selected=indexSelection(selectors[0],series->data->times.size(),depth);if(!selected.scalar)fail("R2540","Use statecoord before selecting ranges of array-state samples.",span);
            const auto width=language::coordinateWidth(series->valueType,series->schema),offset=selected.positions.at(0)*series->data->coordinateCount()+series->offset;
            return nativeCoordinateValue(std::vector<double>(series->data->values.begin()+offset,series->data->values.begin()+offset+width),series->valueType,series->schema,span);
        }
        const auto* matrix=std::get_if<UniversalMatrix>(&base);if(!matrix)return {};
        const auto& source=**matrix;
        if(selectors.empty()||selectors.size()>2)fail("R1901","Use one linear or two row/column native matrix selectors.",span);
        const auto first=indexSelection(selectors[0],selectors.size()==1?source.rows*source.columns:source.rows,depth,selectors.size()==1);IndexSelection second;if(selectors.size()==2)second=indexSelection(selectors[1],source.columns,depth);
        if(first.scalar&&(selectors.size()==1||second.scalar)){const auto n=first.positions[0];return Value(selectors.size()==1?universalMatrixElement(source,n%source.rows,n/source.rows,span):universalMatrixElement(source,n,second.positions[0],span));}
        std::size_t rows=first.rows,columns=first.columns;if(selectors.size()==2){rows=first.positions.size();columns=second.positions.size();}else if(first.all){rows=first.positions.size();columns=1;}else if(source.rows==1||source.columns==1){rows=source.rows==1?1:first.positions.size();columns=source.rows==1?first.positions.size():1;}
        auto result=newUniversalMatrix(source.algebra,rows,columns,span);const auto width=source.algebra->dimension();std::size_t at=0;
        auto copy=[&](std::size_t row,std::size_t column){tick(span);const auto begin=source.values.begin()+(row*source.columns+column)*width;std::copy_n(begin,width,result->values.begin()+at);at+=width;};
        if(selectors.size()==1)for(const auto n:first.positions)copy(n%source.rows,n/source.rows);else for(const auto row:first.positions)for(const auto column:second.positions)copy(row,column);return Value(UniversalMatrix(result));
    }
    std::optional<Value> universalArrayTranspose(const Value& value,bool conjugate,SourceSpan span) {
        const auto* source=std::get_if<UniversalMatrix>(&value);if(!source)return {};auto result=newUniversalMatrix((*source)->algebra,(*source)->columns,(*source)->rows,span);const auto width=(*source)->algebra->dimension();
        universalArrayDispatch(result->rows*result->columns,width*width,span,[&](std::size_t begin,std::size_t end){for(auto n=begin;n<end;++n){auto entry=universalMatrixElement(**source,n%result->columns,n/result->columns,span);if(conjugate)entry=algebra::universal::conjugate(entry);std::copy(entry.coefficients().begin(),entry.coefficients().end(),result->values.begin()+n*width);}});return Value(UniversalMatrix(result));
    }
    Value universalArrayWriteIndex(const UniversalMatrix& source,const std::vector<language::ExprPtr>& selectors,const Expr& replacement,SourceSpan span,std::size_t depth) {
        if(selectors.empty()||selectors.size()>2)fail("R1901","Use one linear or two row/column native matrix selectors.",span);
        const auto first=indexSelection(selectors[0],selectors.size()==1?source->rows*source->columns:source->rows,depth,selectors.size()==1);IndexSelection second;if(selectors.size()==2)second=indexSelection(selectors[1],source->columns,depth);
        const auto value=rangeMatrix(evaluate(replacement,depth+1),replacement.span);const auto right=universalArrayInput(value,source->algebra,span);
        const auto rows=first.positions.size(),columns=selectors.size()==1?1:second.positions.size(),width=source->algebra->dimension();
        if(!right.scalar&&(right.rows*right.columns!=rows*columns||(selectors.size()==2&&(right.rows!=rows||right.columns!=columns))))
            fail("R1901","Native slice assignment requires matching dimensions or one compatible scalar.",span);
        auto result=std::make_shared<UniversalMatrixData>(*source);
        for(std::size_t column=0;column<columns;++column)for(std::size_t row=0;row<rows;++row){
            tick(span);const auto selected=first.positions[row];
            const auto target=selectors.size()==1?(selected%source->rows)*source->columns+selected/source->rows
                :selected*source->columns+second.positions[column];
            const auto replacementCell=right.scalar?0:selectors.size()==1?(row%right.rows)*right.columns+row/right.rows:row*columns+column;
            std::copy_n(right.values.begin()+replacementCell*width,width,result->values.begin()+target*width);
        }
        return UniversalMatrix(result);
    }
    std::optional<Value> universalArrayCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(name=="statecoord"){
            if(arguments.size()!=2||!std::holds_alternative<ArraySeries>(arguments[0]))fail("R2540","Use statecoord(array_state_series,zero_based_coordinate).",span);
        const auto& series=std::get<ArraySeries>(arguments[0]);const auto index=integer(arguments[1],span);const auto width=language::coordinateWidth(series.valueType,series.schema);
            if(index<0||static_cast<std::size_t>(index)>=width)fail("R2540","Array-state coordinate is outside its checked fixed shape.",span);
        return Value(RealSeries{series.data,series.offset+static_cast<std::size_t>(index),false});
        }
        if(arguments.empty()||!std::holds_alternative<UniversalMatrix>(arguments[0]))return {};
        const auto& source=*std::get<UniversalMatrix>(arguments[0]);const auto width=source.algebra->dimension(),count=source.rows*source.columns;
        if(name=="size"){if(arguments.size()==1)return numericResult({static_cast<double>(source.rows),static_cast<double>(source.columns)},1,2,1,span);const auto axis=integer(arguments.at(1),span);if(axis<1)fail("R2540","size dimension must be positive.",span);
        return Value(I(axis==1?source.rows:axis==2?source.columns:1));}
        if(name=="numel"||name=="length")return Value(I(name=="numel"?count:count?std::max(source.rows,source.columns):0));
        if(name=="transpose")return universalArrayTranspose(arguments[0],false,span);
        if(name=="coeff"){const auto coordinate=integer(arguments.at(1),span);if(coordinate<0||static_cast<std::size_t>(coordinate)>=width)fail("R2540","Native matrix coefficient index is outside its entry algebra.",span);std::vector<double> values(count);
        for(std::size_t n=0;n<count;++n)values[n]=source.values[n*width+static_cast<std::size_t>(coordinate)];
        return numericResult(values,source.rows,source.columns,1,span);}
        if(name=="norm"){
            if(source.rows>1&&source.columns>1){if(arguments.size()!=2||!std::holds_alternative<std::string>(arguments[1])||std::get<std::string>(arguments[1])!="fro")fail("R2540","A native algebra matrix norm requires the explicit Frobenius norm; spectral norms need a real coordinate operator.",span);}else if(arguments.size()==2&&!((std::holds_alternative<std::string>(arguments[1])&&std::get<std::string>(arguments[1])=="fro")||((std::holds_alternative<I>(arguments[1])||std::holds_alternative<double>(arguments[1]))&&real(arguments[1],span)==2)))fail("R2540","Native algebra vectors support the coordinate Euclidean/Frobenius norm.",span);
            chargeNumericWork(std::max<std::size_t>(1,source.values.size()),span);double norm=0;for(const auto coefficient:source.values)norm=std::hypot(norm,coefficient);return finite(norm,span);
        }
        if(name=="sum"||name=="mean"){
            const I axis=arguments.size()==1?(source.rows==1?2:1):integer(arguments[1],span);if(axis<1)fail("R2540","Reduction dimension must be positive.",span);
        if(axis>2)return arguments[0];
        const auto extent=axis==1?source.rows:source.columns;if(name=="mean"&&!extent)fail("R2540","Mean requires nonempty reductions.",span);
            auto result=newUniversalMatrix(source.algebra,axis==1?1:source.rows,axis==1?source.columns:1,span);
            universalArrayDispatch(result->rows*result->columns,std::max<std::size_t>(1,extent*width),span,[&](std::size_t begin,std::size_t end){for(auto n=begin;n<end;++n){
                std::vector<long double> totals(width,0);
                for(std::size_t k=0;k<extent;++k){const auto cell=axis==1?k*source.columns+n:n*source.columns+k;for(std::size_t c=0;c<width;++c)totals[c]+=source.values[cell*width+c];}
                for(std::size_t c=0;c<width;++c){const auto wide=name=="mean"?totals[c]/static_cast<long double>(extent):totals[c];
                    if(!std::isfinite(wide)||std::abs(wide)>std::numeric_limits<double>::max())throw algebra::universal::Error("A4002","Algebra reduction produced a nonfinite/unrepresentable coefficient.");
                    const auto value=static_cast<double>(wide);if(wide!=0&&value==0)throw algebra::universal::Error("A4002","A nonzero algebra reduction coefficient underflowed binary64.");
                    result->values[n*width+c]=value;
                }
            }});return Value(UniversalMatrix(result));
        }
        const bool realOutput=name=="scalar"||name=="norm2";const bool elementary=name=="sin"||name=="cos"||name=="exp"||name=="sinh"||name=="cosh";
        if(!realOutput&&!elementary&&name!="pure"&&name!="conj")return {};
        auto result=newUniversalMatrix(source.algebra,source.rows,source.columns,span);std::vector<double> realValues(realOutput?count:0);const auto& kind=source.algebra->builtinKind();const bool quadratic=kind=="real"||kind=="complex"||kind=="quaternion"||kind=="octonion"||kind=="sedenion"||kind=="dual"||kind=="split_complex"||kind=="split_quaternion";
        universalArrayDispatch(count,(width+source.algebra->terms().size())*(elementary&&!quadratic?160:1),span,[&](std::size_t begin,std::size_t end){for(auto n=begin;n<end;++n){auto value=universalMatrixElement(source,n/source.columns,n%source.columns,span);
            if(name=="norm2")realValues[n]=algebra::universal::metricNormSquared(value);else if(name=="scalar")realValues[n]=value.coefficients()[0];
            else{if(name=="sin")value=algebra::universal::sine(value);else if(name=="cos")value=algebra::universal::cosine(value);else if(name=="exp")value=algebra::universal::exponential(value);else if(name=="sinh")value=algebra::universal::hyperbolicSine(value);else if(name=="cosh")value=algebra::universal::hyperbolicCosine(value);else if(name=="conj")value=algebra::universal::conjugate(value);else{auto c=value.coefficients();c[0]=0;value=UniversalElement(source.algebra,std::move(c));}std::copy(value.coefficients().begin(),value.coefficients().end(),result->values.begin()+n*width);}
        }});return realOutput?numericResult(realValues,source.rows,source.columns,1,span):Value(UniversalMatrix(result));
    }
