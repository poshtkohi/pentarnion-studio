    [[noreturn]] void numericalFailure(const NumericModuleError& error,SourceSpan span) {
        fail(error.status==PENTA_NUMERIC_CANCELLED?"R1006":error.status==PENTA_NUMERIC_DIVISION?"R1003":
            error.status==PENTA_NUMERIC_DOMAIN?"R1001":"R1950",error.what(),span);
    }
    bool numericVector(const Matrix& matrix) const {return matrix.rows==1||matrix.columns==1;}
    std::vector<double> numericCoefficients(const Value& value,SourceSpan span) {
        std::vector<double> values;
        if(const auto* array=std::get_if<MatrixValue>(&value)){
            const auto width=(*array)->pentarnion?5u:1u;values.reserve((*array)->values.size()*width);
            for(const auto& entry:(*array)->values){tick(span);for(std::size_t c=0;c<width;++c)values.push_back(entry.coefficients()[c]);}
        }else{
            const auto entry=element(value,span);const auto width=std::holds_alternative<P>(value)?5u:1u;
            for(std::size_t c=0;c<width;++c)values.push_back(entry.coefficients()[c]);
        }
        return values;
    }
    std::uint32_t numericWidth(const Value& value) const {
        if(const auto* array=std::get_if<MatrixValue>(&value))return (*array)->pentarnion?5u:1u;
        return std::holds_alternative<P>(value)?5u:1u;
    }
    Value numericVectorOperation(std::uint32_t operation,const Value& left,const Value* right,
        const Value* alpha,const Matrix& shape,SourceSpan span) {
        auto a=numericCoefficients(left,span);std::vector<double> b,coefficient;
        if(right)b=numericCoefficients(*right,span);
        if(alpha)coefficient=numericCoefficients(*alpha,span);
        const std::uint32_t width=operation==PENTA_VEC_SCALAR_PART||operation==PENTA_VEC_NORM2?1:
            operation==PENTA_VEC_PURE_PART?5:std::max({numericWidth(left),right?numericWidth(*right):1u,alpha?numericWidth(*alpha):1u});
        const auto count=shape.values.size();auto result=newMatrix(shape.rows,shape.columns,width==5,span);
        std::vector<double> output(count*width);
        penta_numeric_vector_request_v1 request{};request.struct_size=sizeof(request);request.operation=operation;request.count=count;
        request.a_count=std::holds_alternative<MatrixValue>(left)?count:1;request.a_width=numericWidth(left);request.a=a.data();
        request.b_count=right?(std::holds_alternative<MatrixValue>(*right)?count:1):0;request.b_width=right?numericWidth(*right):1;request.b=right?b.data():nullptr;
        request.alpha_width=alpha?numericWidth(*alpha):1;request.alpha=alpha?coefficient.data():nullptr;
        request.gamma=context_.gamma();request.output_width=width;request.output=output.data();request.output_capacity=output.size();
        try{NumericModule(options_.numericModuleDirectory).vector(request,parallel_.settings,options_.cancellation,parallel_.statistics);}
        catch(const NumericModuleError& error){numericalFailure(error,span);}
        for(std::size_t n=0;n<count;++n){tick(span);algebra::Coefficients coefficients{};for(std::size_t c=0;c<width;++c)coefficients[c]=output[n*width+c];result->values.emplace_back(context_,coefficients);}
        return MatrixValue(std::move(result));
    }
    std::optional<Value> parallelBinary(const Expr& expression,const Value& left,const Value& right) {
        const auto* a=std::get_if<MatrixValue>(&left);const auto* b=std::get_if<MatrixValue>(&right);
        if(!a&&!b)return {};
        const auto scalar=[](const Value& value){return std::holds_alternative<I>(value)||std::holds_alternative<double>(value)||std::holds_alternative<P>(value);};
        if((!a&&!scalar(left))||(!b&&!scalar(right)))return {};
        const auto& shape=a?**a:**b;
        if(a&&b&&((*a)->rows!=(*b)->rows||(*a)->columns!=(*b)->columns))return {};
        const auto& op=expression.text;std::uint32_t operation=0;
        if(op=="+")operation=PENTA_VEC_ADD;else if(op=="-")operation=PENTA_VEC_SUBTRACT;
        else if(op==".*"||(op=="*"&&!(a&&b)))operation=PENTA_VEC_MULTIPLY;
        else if(op=="./"||(op=="/"&&!b)){
            if(numericWidth(right)!=1)return {};
            operation=PENTA_VEC_DIVIDE;
        }else if(op==".^")operation=PENTA_VEC_POWER;else return {};
        return numericVectorOperation(operation,left,&right,nullptr,shape,expression.span);
    }
    std::optional<Value> parallelUnary(const Expr& expression,const Value& value) {
        const auto* array=std::get_if<MatrixValue>(&value);if(!array)return {};
        if(expression.text=="+")return value;
        if(expression.text!="-")return {};
        return numericVectorOperation(PENTA_VEC_NEGATE,value,nullptr,nullptr,**array,expression.span);
    }
    std::optional<Value> parallelReduction(const Expr& expression,const std::vector<Value>& arguments) {
        if(expression.text!="sum"&&expression.text!="mean")return {};
        const auto* array=std::get_if<MatrixValue>(&arguments.at(0));if(!array||!numericVector(**array))return {};
        const auto& source=**array;const auto span=expression.span;
        const I axis=arguments.size()>1?integer(arguments[1],span):source.rows==1?2:1;
        if(axis<1)return {};
        if(axis>2||(axis==1&&source.rows==1)||(axis==2&&source.columns==1))return arguments[0];
        auto input=numericCoefficients(arguments[0],span);const auto width=source.pentarnion?5u:1u;
        algebra::Coefficients result{};penta_numeric_reduce_request_v1 request{};
        request.struct_size=sizeof(request);request.operation=expression.text=="sum"?PENTA_REDUCE_SUM:PENTA_REDUCE_MEAN;
        request.width=width;request.count=source.values.size();request.a=input.data();request.output=result.data();request.output_capacity=result.size();
        try{NumericModule(options_.numericModuleDirectory).reduce(request,parallel_.settings,options_.cancellation,parallel_.statistics);}
        catch(const NumericModuleError& error){numericalFailure(error,span);}
        auto matrix=newMatrix(1,1,source.pentarnion,span);matrix->values.emplace_back(context_,result);return MatrixValue(std::move(matrix));
    }
    std::optional<Value> parallelCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(auto value=parallelReduction(expression,arguments))return value;
        if(name=="threads"){
            if(arguments.empty())return Value(static_cast<I>(parallel_.settings.threads));
            const auto requested=integer(arguments.at(0),span);if(requested<0||requested>64)fail("R1951","threads requires 0 (automatic) or an integer from 1 to 64.",span);
            parallel_.settings.threads=static_cast<std::size_t>(requested);return Value(I{0});
        }
        if(name=="parallel_info"){
            const auto& info=parallel_.statistics;
            return makeRecord({{"requested",Value(static_cast<I>(parallel_.settings.threads))},
                {"effective",Value(static_cast<I>(ThreadBudget::effectiveThreads(parallel_.settings.threads)))},
                {"threads_used",Value(static_cast<I>(info.threadsUsed))},{"peak_active",Value(static_cast<I>(info.peakActiveThreads))},
                {"last_requested",Value(static_cast<I>(info.requestedThreads))},{"last_effective",Value(static_cast<I>(info.effectiveThreads))},
                {"parallel_calls",Value(static_cast<I>(info.parallelCalls))},{"serial_calls",Value(static_cast<I>(info.serialCalls))},
                {"last_elements",Value(static_cast<I>(info.lastElements))},{"threshold",Value(static_cast<I>(parallel_.settings.serialThreshold))},
                {"module_loaded",Value(static_cast<I>(NumericModule(options_.numericModuleDirectory).loaded()))},
                {"linalg_module_loaded",Value(static_cast<I>(LinalgModule(options_.numericModuleDirectory).loaded()))},
                {"roots_module_loaded",Value(static_cast<I>(roots::Module(options_.numericModuleDirectory).loaded()))},
                {"fitting_module_loaded",Value(static_cast<I>(fitting::Module(options_.numericModuleDirectory).loaded()))},
                {"optimization_module_loaded",Value(static_cast<I>(optimization::Module(options_.numericModuleDirectory).loaded()))},
                {"maps_module_loaded",Value(static_cast<I>(maps::Module(options_.numericModuleDirectory).loaded()))}},span);
        }
        if(name=="axpy"){
            const auto* a=std::get_if<MatrixValue>(&arguments.at(1));const auto* b=std::get_if<MatrixValue>(&arguments.at(2));
            if(!a||!b||!numericVector(**a)||!numericVector(**b)||(*a)->rows!=(*b)->rows||(*a)->columns!=(*b)->columns)
                fail("R1951","axpy(alpha,x,y) requires equal-shaped numeric row or column vectors.",span);
            return numericVectorOperation(PENTA_VEC_AXPY,arguments[1],&arguments[2],&arguments[0],**a,span);
        }
        if(arguments.size()!=1)return {};
        const auto* array=std::get_if<MatrixValue>(&arguments[0]);if(!array)return {};
        std::uint32_t operation=0;
        if(name=="sin")operation=PENTA_VEC_SIN;else if(name=="cos")operation=PENTA_VEC_COS;
        else if(name=="exp")operation=PENTA_VEC_EXP;else if(name=="sinh")operation=PENTA_VEC_SINH;else if(name=="cosh")operation=PENTA_VEC_COSH;
        else if(name=="sqrt")operation=PENTA_VEC_SQRT;else if(name=="tan")operation=PENTA_VEC_TAN;else if(name=="asin")operation=PENTA_VEC_ASIN;
        else if(name=="acos")operation=PENTA_VEC_ACOS;else if(name=="atan")operation=PENTA_VEC_ATAN;else if(name=="tanh")operation=PENTA_VEC_TANH;
        else if(name=="asinh")operation=PENTA_VEC_ASINH;else if(name=="acosh")operation=PENTA_VEC_ACOSH;else if(name=="atanh")operation=PENTA_VEC_ATANH;
        else if(name=="log")operation=PENTA_VEC_LOG;else if(name=="log10")operation=PENTA_VEC_LOG10;else if(name=="log2")operation=PENTA_VEC_LOG2;
        else if(name=="abs")operation=PENTA_VEC_ABS;else if(name=="floor")operation=PENTA_VEC_FLOOR;else if(name=="ceil")operation=PENTA_VEC_CEIL;else if(name=="round")operation=PENTA_VEC_ROUND;
        else if(name=="conj")operation=PENTA_VEC_CONJUGATE;else if(name=="scalar")operation=PENTA_VEC_SCALAR_PART;else if(name=="pure")operation=PENTA_VEC_PURE_PART;else if(name=="norm2")operation=PENTA_VEC_NORM2;
        else if(name=="expm1")operation=PENTA_VEC_EXPM1;else if(name=="log1p")operation=PENTA_VEC_LOG1P;else if(name=="erf")operation=PENTA_VEC_ERF;else if(name=="erfc")operation=PENTA_VEC_ERFC;else if(name=="sign")operation=PENTA_VEC_SIGN;
        else return {};
        return numericVectorOperation(operation,arguments[0],nullptr,nullptr,**array,span);
    }
