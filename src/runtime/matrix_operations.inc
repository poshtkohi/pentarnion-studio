    std::optional<I> indexEnd_;
    bool rangeValue(const Value& value) const {
        return std::holds_alternative<IntegerRange>(value)||std::holds_alternative<RealRange>(value);
    }
    Value rangeMatrix(const Value& value,SourceSpan span) {
        if(!rangeValue(value))return value;
        long double start=0,step=0,stop=0;
        if(const auto* range=std::get_if<IntegerRange>(&value)) {
            start=range->start;step=range->step;stop=range->stop;
        }else{
            const auto& realRange=std::get<RealRange>(value);
            start=realRange.start;step=realRange.step;stop=realRange.stop;
        }
        if(!std::isfinite(start)||!std::isfinite(step)||!std::isfinite(stop)||step==0)
            fail("R1008","Range endpoints and step must be finite, and the step must be nonzero.",span);
        std::size_t count=0;
        if((step>0&&start<=stop)||(step<0&&start>=stop)) {
            const long double intervals=(stop-start)/step;
            const long double tolerance=std::holds_alternative<RealRange>(value)
                ?8*std::numeric_limits<double>::epsilon()*std::max(1.0L,std::abs(intervals)):0;
            const long double wideCount=std::floor(intervals+tolerance)+1;
            if(!std::isfinite(wideCount)||wideCount>std::min<std::size_t>(1000000,options_.maxMatrixElements))
                fail("R1902","Range expansion exceeds the configured array element budget.",span);
            count=static_cast<std::size_t>(wideCount);
        }
        auto result=newMatrix(1,count,false,span);
        for(std::size_t index=0;index<count;++index) {
            tick(span);
            const auto entry=finite(static_cast<double>(start+static_cast<long double>(index)*step),span);
            if(index&&entry==result->values.back().coefficients()[0])
                fail("R1008","The range step is too small to advance its real values.",span);
            result->values.push_back(P::scalar(context_,entry));
        }
        return MatrixValue(std::move(result));
    }
    struct IndexSelection {
        std::vector<std::size_t> positions;
        std::size_t rows=1,columns=1;
        bool scalar=false,all=false;
    };
    std::size_t checkedArrayIndex(const Value& value,std::size_t limit,SourceSpan span) const {
        if(const auto* number=std::get_if<I>(&value)) {
            if(*number<1||static_cast<std::uint64_t>(*number)>limit)
                fail("R1901","An index is outside the array's one-based dimensions.",span);
            return static_cast<std::size_t>(*number-1);
        }
        const double number=real(value,span);
        if(number<1||number!=std::trunc(number)||static_cast<long double>(number)>limit)
            fail("R1901","Indices must be finite positive integers within the array dimensions.",span);
        return static_cast<std::size_t>(number-1);
    }
    IndexSelection indexSelection(const language::ExprPtr& selector,std::size_t limit,std::size_t depth,bool allowMatrixMask=false) {
        IndexSelection result;
        if(selector->kind==ExprKind::AllIndices) {
            result.all=true;result.rows=limit;result.columns=1;
            if(limit>std::min<std::size_t>(1000000,options_.maxMatrixElements))
                fail("R1902","Index selection exceeds the configured array element budget.",selector->span);
            result.positions.reserve(limit);
            for(std::size_t index=0;index<limit;++index){tick(selector->span);result.positions.push_back(index);}
            return result;
        }
        struct RestoreEnd {std::optional<I>& target;std::optional<I> saved;~RestoreEnd(){target=saved;}};
        RestoreEnd restore{indexEnd_,indexEnd_};indexEnd_=static_cast<I>(limit);
        Value value=rangeMatrix(evaluate(*selector,depth+1),selector->span);
        if(const auto* matrix=std::get_if<MatrixValue>(&value)) {
            const auto& indices=**matrix;
            if(indices.logical) {
                if(indices.pentarnion||indices.values.size()!=limit||(!allowMatrixMask&&indices.rows>1&&indices.columns>1))
                    fail("R1901","A logical mask must match the selected extent; row/column masks must be vectors.",selector->span);
                result.rows=0;result.columns=1;
                for(std::size_t index=0;index<indices.values.size();++index){
                    tick(selector->span);
                    // Traverse a full matrix mask in language column-major order.
                    const auto storage=allowMatrixMask?(index%indices.rows)*indices.columns+index/indices.rows:index;
                    if(indices.values[storage].coefficients()[0]!=0)result.positions.push_back(index);
                }
                result.rows=result.positions.size();return result;
            }
            if(indices.pentarnion||(indices.rows>1&&indices.columns>1))
                fail("R1901","An index list must be a real row or column vector.",selector->span);
            result.rows=indices.rows;result.columns=indices.columns;result.positions.reserve(indices.values.size());
            for(const auto& index:indices.values){tick(selector->span);result.positions.push_back(checkedArrayIndex(Value(index.coefficients()[0]),limit,selector->span));}
        }else{
            result.scalar=true;result.positions.push_back(checkedArrayIndex(value,limit,selector->span));
        }
        return result;
    }
    struct IndexedShape {std::size_t rows=0,columns=0;bool pentarnion=false;};
    IndexedShape indexedShape(const Value& value,SourceSpan span) const {
        if(const auto* matrix=std::get_if<MatrixValue>(&value))return {(*matrix)->rows,(*matrix)->columns,(*matrix)->pentarnion};
        if(const auto* series=std::get_if<RealSeries>(&value))return {series->data->times.size(),1,false};
        if(const auto* series=std::get_if<PentSeries>(&value))return {series->data->times.size(),1,true};
        fail("R1901","Only numeric arrays and sampled solution series support indexing.",span);
    }
    P indexedElement(const Value& value,std::size_t row,std::size_t column,SourceSpan span) const {
        if(const auto* matrix=std::get_if<MatrixValue>(&value))return (*matrix)->values.at(row*(*matrix)->columns+column);
        if(const auto* series=std::get_if<RealSeries>(&value)) {
            const double number=series->times?series->data->times.at(row)
                :series->data->values.at(row*series->data->coordinateCount()+series->offset);
            return P::scalar(context_,number);
        }
        if(const auto* series=std::get_if<PentSeries>(&value)) {
            algebra::Coefficients coefficients{};
            for(std::size_t component=0;component<5;++component)
                coefficients[component]=series->data->values.at(row*series->data->coordinateCount()+series->offset+component);
            return P(context_,coefficients);
        }
        fail("R1901","The indexed value is not an array or series.",span);
    }
    Value readIndex(const Value& base,const std::vector<language::ExprPtr>& selectors,SourceSpan span,std::size_t depth) {
        if(auto tensor=tensorReadIndex(base,selectors,span,depth))return *tensor;
        if(auto array=universalArrayReadIndex(base,selectors,span,depth))return *array;
        if(const auto* series=std::get_if<UniversalSeries>(&base)){
            if(selectors.size()!=1)fail("R2530","Use an algebra series with one scalar sample index.",span);
            const auto selected=indexSelection(selectors[0],series->data->times.size(),depth);
            if(!selected.scalar)fail("R2530","Project coefficients with coeff(series,n) before selecting an array of samples.",span);
            const auto start=selected.positions.at(0)*series->data->coordinateCount()+series->offset;
            if(series->complex)return std::complex<double>(series->data->values.at(start),series->data->values.at(start+1));
            const auto width=series->algebra->dimension();
            return UniversalElement(series->algebra,std::vector<double>(series->data->values.begin()+start,series->data->values.begin()+start+width));
        }
        const auto shape=indexedShape(base,span);
        if(selectors.empty()||selectors.size()>2)fail("R1901","Use one linear selector or two row/column selectors.",span);
        auto first=indexSelection(selectors[0],selectors.size()==1?shape.rows*shape.columns:shape.rows,depth,selectors.size()==1);
        IndexSelection second;
        if(selectors.size()==2)second=indexSelection(selectors[1],shape.columns,depth);
        if(first.scalar&&(selectors.size()==1||second.scalar)) {
            const auto offset=first.positions[0];
            const P entry=selectors.size()==1?indexedElement(base,offset%shape.rows,offset/shape.rows,span)
                :indexedElement(base,offset,second.positions[0],span);
            return shape.pentarnion?Value(entry):Value(entry.coefficients()[0]);
        }
        std::size_t rows=first.rows,columns=first.columns;
        if(selectors.size()==2){rows=first.positions.size();columns=second.positions.size();}
        else if(first.all){rows=first.positions.size();columns=1;}
        else if(shape.rows==1||shape.columns==1) {
            rows=shape.rows==1?1:first.positions.size();columns=shape.rows==1?first.positions.size():1;
        }
        auto result=newMatrix(rows,columns,shape.pentarnion,span);
        if(const auto* matrix=std::get_if<MatrixValue>(&base))result->logical=(*matrix)->logical;
        if(selectors.size()==1) {
            for(const auto offset:first.positions){tick(span);result->values.push_back(indexedElement(base,offset%shape.rows,offset/shape.rows,span));}
        }else{
            for(const auto row:first.positions)for(const auto column:second.positions){tick(span);result->values.push_back(indexedElement(base,row,column,span));}
        }
        return MatrixValue(std::move(result));
    }
    MatrixValue writeIndex(const MatrixValue& source,const std::vector<language::ExprPtr>& selectors,
                           const Expr& replacement,SourceSpan span,std::size_t depth) {
        if(selectors.empty()||selectors.size()>2)fail("R1901","Use one linear selector or two row/column selectors.",span);
        const auto first=indexSelection(selectors[0],selectors.size()==1?source->values.size():source->rows,depth,selectors.size()==1);
        IndexSelection second;
        if(selectors.size()==2)second=indexSelection(selectors[1],source->columns,depth);
        const Value right=rangeMatrix(evaluate(replacement,depth+1),span);
        const auto* array=std::get_if<MatrixValue>(&right);
        const auto rows=first.positions.size(),columns=selectors.size()==1?1:second.positions.size();
        if(array&&((*array)->values.size()!=rows*columns || (selectors.size()==2&&((*array)->rows!=rows||(*array)->columns!=columns))))
            fail("R1901","Slice assignment requires matching dimensions or one numeric scalar.",span);
        if(!source->pentarnion&&((array&&(*array)->pentarnion)||std::holds_alternative<P>(right)))
            fail("R1901","A real array cannot receive Pentarnion entries.",span);
        auto result=std::make_shared<Matrix>(*source);
        for(std::size_t column=0;column<columns;++column)for(std::size_t row=0;row<rows;++row) {
            tick(span);const auto selected=first.positions[row];
            const auto target=selectors.size()==1?(selected%source->rows)*source->columns+selected/source->rows
                :selected*source->columns+second.positions[column];
            if(array) {
                const auto offset=selectors.size()==1?(row%(*array)->rows)*(*array)->columns+row/(*array)->rows
                    :row*columns+column;
                result->values[target]=(*array)->values[offset];
            }else result->values[target]=source->pentarnion?element(right,span):P::scalar(context_,real(right,span));
            if(source->logical)result->values[target]=P::scalar(context_,result->values[target].coefficients()[0]!=0?1:0);
        }
        return MatrixValue(std::move(result));
    }
    Value matrixLiteral(const Expr& expression,std::size_t depth) {
        // Literal cells may themselves be scalar, range or array values. Expand
        // and concatenate blocks without flattening either row or column shape.
        std::vector<MatrixValue> cells;cells.reserve(expression.children.size());
        std::size_t retainedElements=0;
        const auto limit=std::min<std::size_t>(1000000,options_.maxMatrixElements);
        for(const auto& child:expression.children) {
            Value value=rangeMatrix(evaluate(*child,depth+1),child->span);
            if(const auto* matrix=std::get_if<MatrixValue>(&value)) {
                if((*matrix)->values.size()>limit-retainedElements)
                    fail("R1902","Expanded matrix literal exceeds the configured element budget.",child->span);
                retainedElements+=(*matrix)->values.size();cells.push_back(*matrix);
            }else {
                if(retainedElements==limit)fail("R1902","Expanded matrix literal exceeds the configured element budget.",child->span);
                ++retainedElements;
                auto cell=newMatrix(1,1,std::holds_alternative<P>(value),child->span);cell->values.push_back(element(value,child->span));cells.push_back(std::move(cell));
            }
        }
        if(cells.empty())return MatrixValue(newMatrix(0,0,false,expression.span));
        std::vector<std::size_t> rowWidths=expression.rowWidths;
        if(rowWidths.empty())for(std::size_t row=0;row<expression.rows;++row)rowWidths.push_back(expression.columns);
        std::size_t offset=0,totalRows=0,totalColumns=0;bool pentarnion=false;
        struct BlockRow {std::size_t offset,count,height,width;};std::vector<BlockRow> blocks;
        for(const auto count:rowWidths) {
            if(!count||offset+count>cells.size())fail("R1901","Malformed matrix literal row.",expression.span);
            const auto height=cells[offset]->rows;std::size_t width=0;
            for(std::size_t n=0;n<count;++n){const auto& cell=*cells[offset+n];if(cell.rows!=height)fail("R1901","Blocks in a literal row must have equal heights.",expression.span);width+=cell.columns;pentarnion=pentarnion||cell.pentarnion;}
            if(!blocks.empty()&&width!=totalColumns)fail("R1901","Expanded literal rows must have equal widths.",expression.span);
            totalColumns=width;totalRows+=height;blocks.push_back({offset,count,height,width});offset+=count;
        }
        if(offset!=cells.size())fail("R1901","Malformed matrix literal dimensions.",expression.span);
        auto result=newMatrix(totalRows,totalColumns,pentarnion,expression.span);
        for(const auto& block:blocks)for(std::size_t row=0;row<block.height;++row)for(std::size_t n=0;n<block.count;++n) {
            const auto& cell=*cells[block.offset+n];
            for(std::size_t column=0;column<cell.columns;++column){tick(expression.span);result->values.push_back(cell.values[row*cell.columns+column]);}
        }
        return MatrixValue(std::move(result));
    }
    std::shared_ptr<Matrix> newMatrix(std::size_t rows,std::size_t columns,bool pentarnion,SourceSpan span) {
        const auto limit=std::min<std::size_t>(1000000,options_.maxMatrixElements);
        if(rows>100000 || columns>100000 || (columns && rows>limit/columns))
            fail("R1902","Matrix dimensions exceed the configured element budget.",span);
        auto result=std::make_shared<Matrix>();result->rows=rows;result->columns=columns;result->pentarnion=pentarnion;
        result->values.reserve(rows*columns);return result;
    }
    void checkMatrixWorkspace(const std::string& target,const Value& candidate,SourceSpan span) {
        const auto limit=std::min<std::size_t>(8000000,options_.maxMatrixCoordinates);
        std::size_t used=0;std::unordered_set<const Matrix*> retained;
        const auto count=[&](const Value& value) {
            if(const auto* matrix=std::get_if<MatrixValue>(&value)) {
                if(!retained.insert(matrix->get()).second)return;
                const auto coordinates=(*matrix)->values.size()*((*matrix)->pentarnion?5:1);
                if(coordinates>limit-used)fail("R1902","Matrix workspace exceeds the retained-coordinate budget.",span);
                used+=coordinates;
            }
        };
        count(candidate);
        for(const auto& [name,value]:variables_)if(name!=target)count(value);
        for(const auto* frame:outerFrames_)
            for(const auto& [name,value]:*frame){static_cast<void>(name);count(value);}
    }
    std::size_t dimension(const Value& value,SourceSpan span) const {
        const auto result=integer(value,span);
        if(result<0 || result>100000)fail("R1901","Array dimensions must be integers in 0..100000.",span);
        return static_cast<std::size_t>(result);
    }
    std::size_t matrixIndex(const Matrix& matrix,const std::vector<Value>& indices,SourceSpan span) const {
        if(indices.empty()||indices.size()>2)fail("R1901","Use one linear index or two row/column indices.",span);
        const auto first=integer(indices[0],span);
        if(indices.size()==1) {
            if(first<1 || static_cast<std::uint64_t>(first)>matrix.values.size())fail("R1901","Matrix linear index is outside 1..numel(A).",span);
            const auto linear=static_cast<std::size_t>(first-1);
            return (linear%matrix.rows)*matrix.columns+linear/matrix.rows;
        }
        const auto second=integer(indices[1],span);
        if(first<1 || second<1 || static_cast<std::uint64_t>(first)>matrix.rows || static_cast<std::uint64_t>(second)>matrix.columns)
            fail("R1901","Matrix row or column index is outside its dimensions.",span);
        return static_cast<std::size_t>(first-1)*matrix.columns+static_cast<std::size_t>(second-1);
    }
    Value transposeValue(const Value& value,bool conjugate,SourceSpan span) {
        if(auto array=universalArrayTranspose(value,conjugate,span))return *array;
        if(auto output=linalgTranspose(value,conjugate,span))return *output;
        if(const auto* source=std::get_if<MatrixValue>(&value)) {
            const auto& matrix=**source;auto result=newMatrix(matrix.columns,matrix.rows,matrix.pentarnion,span);
            result->logical=matrix.logical;
            for(std::size_t row=0;row<matrix.columns;++row)for(std::size_t column=0;column<matrix.rows;++column) {
                tick(span);const auto& entry=matrix.values[column*matrix.columns+row];
                result->values.push_back(conjugate?algebra::conjugate(entry):entry);
            }
            return MatrixValue(std::move(result));
        }
        if(conjugate && std::holds_alternative<P>(value))return algebra::conjugate(std::get<P>(value));
        return value;
    }
    Value matrixBinary(const Expr& expression,const Value& left,const Value& right) {
        const auto span=expression.span;const auto& op=expression.text;
        const auto* lm=std::get_if<MatrixValue>(&left);const auto* rm=std::get_if<MatrixValue>(&right);
        const bool pentarnion=(lm?(*lm)->pentarnion:std::holds_alternative<P>(left)) || (rm?(*rm)->pentarnion:std::holds_alternative<P>(right));
        if(op=="*" && lm && rm) {
            const auto& a=**lm;const auto& b=**rm;
            if(a.columns!=b.rows)fail("R1901","Matrix multiplication requires columns(A) == rows(B).",span);
            auto result=newMatrix(a.rows,b.columns,pentarnion,span);
            Expr multiply;multiply.text="*";multiply.span=span;Expr add;add.text="+";add.span=span;
            for(std::size_t row=0;row<a.rows;++row)for(std::size_t column=0;column<b.columns;++column) {
                tick(span);
                Value sum=pentarnion?Value(P::scalar(context_,0)):Value(0.0);
                for(std::size_t k=0;k<a.columns;++k) {
                    tick(span);
                    const Value av=a.pentarnion?Value(a.values[row*a.columns+k]):Value(a.values[row*a.columns+k].coefficients()[0]);
                    const Value bv=b.pentarnion?Value(b.values[k*b.columns+column]):Value(b.values[k*b.columns+column].coefficients()[0]);
                    // Ordered binary entry products; never reassociate matrix chains.
                    sum=binary(add,sum,binary(multiply,av,bv));
                }
                result->values.push_back(element(sum,span));
            }
            return MatrixValue(std::move(result));
        }
        if(op=="\\" || op=="^" || (op=="/" && (rm || std::holds_alternative<P>(right))))
            fail("R1901","Matrix solves, inverses and matrix powers are not supported. Use elementwise operators explicitly.",span);
        const auto& shape=lm?**lm:**rm;
        if(lm&&rm && ((*lm)->rows!=(*rm)->rows || (*lm)->columns!=(*rm)->columns))
            fail("R1901","Elementwise matrix operations require equal dimensions.",span);
        if(op=="./" && (rm?(*rm)->pentarnion:std::holds_alternative<P>(right)))
            fail("R1901","Elementwise matrix division currently requires real divisors.",span);
        auto result=newMatrix(shape.rows,shape.columns,pentarnion,span);
        Expr scalar=expression;if(op.size()==2 && op[0]=='.')scalar.text=op.substr(1);
        for(std::size_t index=0;index<shape.values.size();++index) {
            tick(span);
            const auto entry=[&](const Value& original,const MatrixValue* matrix)->Value {
                if(!matrix)return original;
                const auto& value=(*matrix)->values[index];
                return (*matrix)->pentarnion?Value(value):Value(value.coefficients()[0]);
            };
            Value av=entry(left,lm),bv=entry(right,rm);
            if(scalar.text=="^" && std::holds_alternative<P>(av)) {
                const double exponent=real(bv,span);
                if(exponent!=std::trunc(exponent) || exponent < -100000 || exponent >100000)
                    fail("R1901","Pentarnion elementwise powers require integer exponents in -100000..100000.",span);
                bv=static_cast<I>(exponent);
            }
            result->values.push_back(element(binary(scalar,av,bv),span));
        }
        return MatrixValue(std::move(result));
    }
    std::optional<Value> matrixCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(name=="sum"||name=="mean"||name=="min"||name=="max") {
            const bool minimum=name=="min",maximum=name=="max";
            const Value input=rangeMatrix(arguments.at(0),span);
            const auto isArray=[](const Value& value){return std::holds_alternative<MatrixValue>(value)||std::holds_alternative<RealSeries>(value)||std::holds_alternative<PentSeries>(value);};
            if((minimum||maximum)&&arguments.size()==2) {
                const Value other=rangeMatrix(arguments[1],span);
                if(!isArray(input)&&!isArray(other)) {
                    const auto a=real(input,span),b=real(other,span);return Value(minimum?std::min(a,b):std::max(a,b));
                }
                const bool leftArray=isArray(input),rightArray=isArray(other);
                const auto shape=indexedShape(leftArray?input:other,span);
                if(shape.pentarnion)fail("R1901","min and max require real entries.",span);
                if(leftArray&&rightArray) {
                    const auto otherShape=indexedShape(other,span);
                    if(otherShape.pentarnion||shape.rows!=otherShape.rows||shape.columns!=otherShape.columns)
                        fail("R1901","Pairwise min/max require real equal-size arrays or one scalar.",span);
                }
                auto result=newMatrix(shape.rows,shape.columns,false,span);
                for(std::size_t row=0;row<shape.rows;++row)for(std::size_t column=0;column<shape.columns;++column) {
                    tick(span);const auto a=leftArray?indexedElement(input,row,column,span).coefficients()[0]:real(input,span);
                    const auto b=rightArray?indexedElement(other,row,column,span).coefficients()[0]:real(other,span);
                    result->values.push_back(P::scalar(context_,minimum?std::min(a,b):std::max(a,b)));
                }
                return Value(MatrixValue(std::move(result)));
            }
            I axis=1;
            if(arguments.size()>=2) {
                if((minimum||maximum)&&arguments.size()==3) {
                    const auto* empty=std::get_if<MatrixValue>(&arguments[1]);
                    if(!empty||!(*empty)->values.empty())fail("R1901","Dimension reduction uses min/max(A,[],dimension).",span);
                    axis=integer(arguments[2],span);
                }else axis=integer(arguments[1],span);
                if(axis<1)fail("R1901","A reduction dimension must be a positive integer.",span);
            }
            if(!isArray(input)) {
                if(minimum||maximum)static_cast<void>(real(input,span));
                return input;
            }
            const auto shape=indexedShape(input,span);
            if((minimum||maximum)&&shape.pentarnion)fail("R1901","min and max require real entries.",span);
            if(arguments.size()==1)axis=shape.rows==1?2:1;
            if(axis>2)return input;
            const auto count=axis==1?shape.rows:shape.columns;
            std::size_t rows=axis==1?1:shape.rows,columns=axis==1?shape.columns:1;
            if(shape.rows==0&&shape.columns==0&&name=="sum"){rows=1;columns=1;}
            if((minimum||maximum)&&count==0){rows=axis==1?0:rows;columns=axis==2?0:columns;}
            if(name=="mean"&&count==0&&rows!=0&&columns!=0)fail("R1901","The mean of an empty reduction is outside the finite numeric domain.",span);
            auto result=newMatrix(rows,columns,shape.pentarnion,span);
            for(std::size_t row=0;row<rows;++row)for(std::size_t column=0;column<columns;++column) {
                algebra::Coefficients coefficients{};std::array<long double,5> totals{};double extremum=0;
                for(std::size_t index=0;index<count;++index) {
                    tick(span);const auto entry=indexedElement(input,axis==1?index:row,axis==1?column:index,span);
                    if(minimum||maximum) {
                        const double number=entry.coefficients()[0];
                        if(index==0)extremum=number;
                        else extremum=minimum?std::min(extremum,number):std::max(extremum,number);
                    }else for(std::size_t component=0;component<(shape.pentarnion?5u:1u);++component)totals[component]+=entry.coefficients()[component];
                }
                if(minimum||maximum)coefficients[0]=extremum;
                else for(std::size_t component=0;component<(shape.pentarnion?5u:1u);++component)
                    coefficients[component]=finite(static_cast<double>(name=="mean"?totals[component]/count:totals[component]),span);
                result->values.emplace_back(context_,coefficients);
            }
            return Value(MatrixValue(std::move(result)));
        }
        if(name=="rng") {
            if(const auto* text=std::get_if<std::string>(&arguments.at(0))) {
                if(*text!="default")fail("R1903","rng accepts a nonnegative integer seed or \"default\".",span);
                random_.engine.seed(Random::defaultSeed);
            } else {
                const I seed=integer(arguments.at(0),span);if(seed<0)fail("R1903","rng seed must be nonnegative.",span);
                random_.engine.seed(static_cast<std::uint64_t>(seed));
            }
            return Value(I(0));
        }
        if(name=="rand" || name=="randn" || name=="randi" || name=="zeros" || name=="ones" || name=="eye") {
            const bool integers=name=="randi";const auto firstDimension=integers?1u:0u;
            I maximum=1;
            if(integers) {
                maximum=integer(arguments.at(0),span);
                if(maximum<1 || maximum>9007199254740991LL)fail("R1903","randi upper bound must be an integer in 1..2^53-1.",span);
            }
            const auto next=[&]()->Value {
                tick(span);
                if(integers) {
                    const auto bound=static_cast<std::uint64_t>(maximum);
                    const auto threshold=(std::uint64_t(0)-bound)%bound;
                    std::uint64_t sample;
                    do {tick(span);sample=random_.engine();}while(sample<threshold);
                    return static_cast<I>(1+sample%bound);
                }
                if(name=="rand")return random_.uniform();
                if(name=="randn") {
                    const double u1=1.0-random_.uniform(),u2=random_.uniform();
                    return finite(std::sqrt(-2*std::log(u1))*std::cos(6.283185307179586476925286766559*u2),span);
                }
                return name=="ones"?1.0:0.0;
            };
            if(arguments.size()==firstDimension)return next();
            const auto rows=dimension(arguments[firstDimension],span);
            const auto columns=arguments.size()==firstDimension+1?rows:dimension(arguments[firstDimension+1],span);
            auto result=newMatrix(rows,columns,false,span);
            for(std::size_t row=0;row<rows;++row)for(std::size_t column=0;column<columns;++column) {
                const double value=name=="eye"?(row==column?1.0:0.0):real(next(),span);
                if(name=="eye")tick(span);
                result->values.push_back(P::scalar(context_,value));
            }
            return Value(MatrixValue(std::move(result)));
        }
        if(name=="linspace") {
            const double start=real(arguments.at(0),span),stop=real(arguments.at(1),span);
            const auto count=arguments.size()==3?dimension(arguments[2],span):std::size_t(100);
            auto result=newMatrix(1,count,false,span);
            for(std::size_t index=0;index<count;++index) {
                tick(span);const auto ratio=count==1?1.0L:static_cast<long double>(index)/(count-1);
                const auto value=static_cast<double>((1-ratio)*static_cast<long double>(start)+ratio*static_cast<long double>(stop));
                result->values.push_back(P::scalar(context_,finite(value,span)));
            }
            return Value(MatrixValue(std::move(result)));
        }
        if(name=="transpose")return transposeValue(arguments.at(0),false,span);
        if(name=="size" || name=="numel" || name=="length") {
            const Value input=rangeMatrix(arguments.at(0),span);
            const bool array=std::holds_alternative<MatrixValue>(input)||std::holds_alternative<RealSeries>(input)||std::holds_alternative<PentSeries>(input);
            const auto shape=array?indexedShape(input,span):IndexedShape{1,1,false};
            const std::size_t rows=shape.rows,columns=shape.columns;
            if(name=="numel")return Value(static_cast<I>(rows*columns));
            if(name=="length")return Value(static_cast<I>(rows&&columns?std::max(rows,columns):0));
            if(arguments.size()==2) {
                const auto axis=integer(arguments[1],span);if(axis<1)fail("R1901","size dimension must be a positive integer.",span);
                return Value(static_cast<I>(axis==1?rows:axis==2?columns:1));
            }
            auto result=newMatrix(1,2,false,span);
            result->values.push_back(P::scalar(context_,static_cast<double>(rows)));result->values.push_back(P::scalar(context_,static_cast<double>(columns)));
            return Value(MatrixValue(std::move(result)));
        }
        if(arguments.empty() || !std::holds_alternative<MatrixValue>(arguments[0]))return {};
        if(name=="coeff" || name=="conj" || name=="scalar" || name=="pure" || name=="norm2" ||
           name=="sin" || name=="cos" || name=="exp" || name=="sinh" || name=="cosh" ||
           name=="sqrt" || name=="tan" || name=="asin" || name=="acos" || name=="atan" || name=="tanh" ||
           name=="asinh" || name=="acosh" || name=="atanh" || name=="log" || name=="log10" || name=="log2" ||
           name=="abs" || name=="floor" || name=="ceil" || name=="round") {
            const auto& matrix=*std::get<MatrixValue>(arguments[0]);
            const bool pentarnion=expression.type==Type::PentarnionMatrix;
            auto result=newMatrix(matrix.rows,matrix.columns,pentarnion,span);
            for(const auto& entry:matrix.values) {
                tick(span);std::vector<Value> scalarArguments=arguments;
                scalarArguments[0]=matrix.pentarnion?Value(entry):Value(entry.coefficients()[0]);
                result->values.push_back(element(call(expression,scalarArguments),span));
            }
            return Value(MatrixValue(std::move(result)));
        }
        return {};
    }
