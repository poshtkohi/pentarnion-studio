    struct NumericArray {
        std::size_t rows=0,columns=0,components=1;
        bool logical=false,scalar=false;
        std::vector<double> values;
    };
    void chargeNumericWork(std::size_t count,SourceSpan span) {
        tick(span);
        if(count>1){if(count-1>options_.maxOperations-result_.operations)fail("R1005","Numerical work exceeds the configured operation budget.",span);result_.operations+=count-1;}
    }
    NumericArray numericArray(const Value& original,SourceSpan span) {
        const Value value=rangeMatrix(original,span);NumericArray result;
        const bool array=std::holds_alternative<MatrixValue>(value)||std::holds_alternative<RealSeries>(value)||std::holds_alternative<PentSeries>(value);
        if(!array){result.rows=result.columns=1;result.scalar=true;result.components=std::holds_alternative<P>(value)?5:1;const auto entry=element(value,span);for(std::size_t c=0;c<result.components;++c)result.values.push_back(entry.coefficients()[c]);return result;}
        const auto shape=indexedShape(value,span);result.rows=shape.rows;result.columns=shape.columns;result.components=shape.pentarnion?5:1;
        if(const auto* matrix=std::get_if<MatrixValue>(&value))result.logical=(*matrix)->logical;
        if(result.rows>8000000||result.columns>100000||(result.columns&&result.rows>8000000/result.columns/result.components))fail("R1902","Numerical input exceeds its coordinate budget.",span);
        result.values.reserve(result.rows*result.columns*result.components);
        for(std::size_t row=0;row<result.rows;++row)for(std::size_t column=0;column<result.columns;++column){tick(span);const auto entry=indexedElement(value,row,column,span);for(std::size_t c=0;c<result.components;++c)result.values.push_back(finite(entry.coefficients()[c],span));}
        return result;
    }
    Value numericResult(const std::vector<double>& values,std::size_t rows,std::size_t columns,std::size_t components,SourceSpan span,bool scalar=false,bool logical=false) {
        if(scalar){if(values.size()!=components)fail("R1950","Invalid numerical scalar result layout.",span);if(components==1)return finite(values[0],span);algebra::Coefficients coefficients{};for(std::size_t c=0;c<5;++c)coefficients[c]=finite(values[c],span);return P(context_,coefficients);}
        auto result=newMatrix(rows,columns,components==5,span);result->logical=logical;
        if(values.size()!=rows*columns*components)fail("R1950","Invalid numerical array result layout.",span);
        for(std::size_t n=0;n<rows*columns;++n){tick(span);algebra::Coefficients coefficients{};for(std::size_t c=0;c<components;++c)coefficients[c]=finite(values[n*components+c],span);result->values.emplace_back(context_,coefficients);}
        return MatrixValue(std::move(result));
    }
    std::vector<double> numericVector(const Value& value,SourceSpan span,bool allowEmpty=true) {
        const auto array=numericArray(value,span);if(array.components!=1||(array.rows>1&&array.columns>1)||(!allowEmpty&&array.values.empty()))fail("R1950","Expected a real row/column vector.",span);return array.values;
    }
    std::vector<double> numericCoordinates(const Value* spacing,std::size_t extent,SourceSpan span) {
        if(spacing&&(std::holds_alternative<MatrixValue>(*spacing)||std::holds_alternative<RealSeries>(*spacing)||rangeValue(*spacing))){auto coordinates=numericVector(*spacing,span);if(coordinates.size()!=extent)fail("R1950","Coordinate vector length must match the selected array dimension.",span);return coordinates;}
        const double step=spacing?real(*spacing,span):1;
        if(step==0)fail("R1950","Grid spacing must be nonzero.",span);
        std::vector<double> result;result.reserve(extent);
        for(std::size_t n=0;n<extent;++n){tick(span);result.push_back(finite(static_cast<double>(static_cast<long double>(n)*step),span));}
        return result;
    }
    void runCalculus(penta_numeric_calculus_request_v1& request,std::vector<double>& result,SourceSpan span,std::size_t cost) {
        if(request.coordinate_count>std::numeric_limits<std::size_t>::max()-cost)fail("R1005","Numerical coordinate work exceeds its operation budget.",span);
        chargeNumericWork(cost+static_cast<std::size_t>(request.coordinate_count),span);request.struct_size=sizeof(request);request.output=result.data();request.output_capacity=result.size();
        try{NumericModule(options_.numericModuleDirectory).calculus(request,parallel_.settings,options_.cancellation,parallel_.statistics);}
        catch(const NumericModuleError& error){fail(error.status==PENTA_NUMERIC_CANCELLED?"R1006":"R1950",error.what(),span);}
    }
    NumericArray differentiateNumeric(const NumericArray& input,std::size_t axis,const Value* spacing,unsigned order,SourceSpan span) {
        const auto extent=axis==0?input.rows:input.columns;const auto coordinates=numericCoordinates(spacing,extent,span);
        NumericArray result=input;result.logical=false;result.values.assign(input.values.size(),0);
        penta_numeric_calculus_request_v1 request{};request.operation=order==1?PENTA_CALC_DERIVATIVE1:PENTA_CALC_DERIVATIVE2;
        request.rows=input.rows;request.columns=input.columns;request.components=input.components;request.axis=static_cast<uint32_t>(axis);request.value_stride=input.components;
        request.data=input.values.data();request.data_capacity=input.values.size();request.coordinates=coordinates.data();request.coordinate_count=coordinates.size();
        runCalculus(request,result.values,span,input.values.size());return result;
    }
    std::optional<Value> foundationNumericCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(name=="reshape") {
            const auto input=numericArray(arguments.at(0),span);std::size_t rows=0,columns=0;bool inferRows=false,inferColumns=false;
            auto shapeDimension=[&](const Value& value,bool& infer){if(const auto* matrix=std::get_if<MatrixValue>(&value);matrix&&(*matrix)->values.empty()){infer=true;return std::size_t(0);}return dimension(value,span);};
            if(arguments.size()==2){const auto dimensions=numericVector(arguments[1],span);if(dimensions.size()!=2)fail("R1950","reshape shape vectors currently require exactly two dimensions.",span);
                for(const auto d:dimensions)if(d<0||d>100000||d!=std::trunc(d))fail("R1950","reshape dimensions must be nonnegative integers.",span);
                rows=static_cast<std::size_t>(dimensions[0]);columns=static_cast<std::size_t>(dimensions[1]);}
            else{rows=shapeDimension(arguments.at(1),inferRows);columns=shapeDimension(arguments.at(2),inferColumns);}
            const auto count=input.rows*input.columns;
            if(inferRows&&inferColumns)fail("R1950","Only one reshape dimension may be inferred.",span);
            if(inferRows){if(!columns||count%columns)fail("R1950","Cannot infer reshape rows from the element count and columns.",span);rows=count/columns;}
            if(inferColumns){if(!rows||count%rows)fail("R1950","Cannot infer reshape columns from the element count and rows.",span);columns=count/rows;}
            auto result=newMatrix(rows,columns,input.components==5,span);result->logical=input.logical;
            if(rows*columns!=count)fail("R1950","reshape must preserve the number of array elements.",span);
            for(std::size_t row=0;row<rows;++row)for(std::size_t column=0;column<columns;++column){tick(span);const auto linear=row+column*rows;const auto source=(linear%input.rows)*input.columns+linear/input.rows;algebra::Coefficients coefficients{};for(std::size_t c=0;c<input.components;++c)coefficients[c]=input.values[source*input.components+c];result->values.emplace_back(context_,coefficients);}
            return MatrixValue(std::move(result));
        }
        if(name=="diag") {
            const auto input=numericArray(arguments.at(0),span);const I offset=arguments.size()>1?integer(arguments[1],span):0;
            if(offset<-100000||offset>100000)fail("R1950","diag offset exceeds the bounded matrix dimensions.",span);
            const auto shift=static_cast<std::size_t>(offset<0?-offset:offset);const bool vector=input.rows<=1||input.columns<=1;
            if(vector){const auto count=input.rows*input.columns;auto result=newMatrix(count+shift,count+shift,input.components==5,span);result->logical=input.logical;result->values.assign(result->rows*result->columns,P::scalar(context_,0));
                for(std::size_t n=0;n<count;++n){tick(span);algebra::Coefficients coefficients{};for(std::size_t c=0;c<input.components;++c)coefficients[c]=input.values[n*input.components+c];const auto row=n+(offset<0?shift:0),column=n+(offset>0?shift:0);result->values[row*result->columns+column]=P(context_,coefficients);}return MatrixValue(std::move(result));}
            const auto startRow=offset<0?shift:0,startColumn=offset>0?shift:0;
            const auto count=startRow<input.rows&&startColumn<input.columns?std::min(input.rows-startRow,input.columns-startColumn):0;
            std::vector<double> result;result.reserve(count*input.components);
            for(std::size_t n=0;n<count;++n){tick(span);const auto source=((startRow+n)*input.columns+startColumn+n)*input.components;for(std::size_t c=0;c<input.components;++c)result.push_back(input.values[source+c]);}
            return numericResult(result,count,1,input.components,span,false,input.logical);
        }
        if(name=="meshgrid"||name=="ndgrid") {
            const auto x=numericVector(arguments.at(0),span);const auto y=arguments.size()>1?numericVector(arguments[1],span):x;
            if(name=="ndgrid"&&arguments.size()==1)return numericResult(x,x.size(),1,1,span);
            const auto rows=name=="meshgrid"?y.size():x.size(),columns=name=="meshgrid"?x.size():y.size();
            auto X=newMatrix(rows,columns,false,span),Y=newMatrix(rows,columns,false,span);
            for(std::size_t row=0;row<rows;++row)for(std::size_t column=0;column<columns;++column){tick(span);X->values.push_back(P::scalar(context_,x[name=="meshgrid"?column:row]));Y->values.push_back(P::scalar(context_,y[name=="meshgrid"?row:column]));}
            return makeTuple({Value(MatrixValue(std::move(X))),Value(MatrixValue(std::move(Y)))},span);
        }
        if(name=="logical"||name=="find") {
            const auto input=numericArray(arguments.at(0),span);
            if(name=="logical"){
                if(input.components!=1)fail("R1950","logical requires real values; compare or select Pentarnion coefficients explicitly.",span);
                auto result=newMatrix(input.rows,input.columns,false,span);result->logical=true;
                for(const auto value:input.values){tick(span);result->values.push_back(P::scalar(context_,value!=0?1:0));}return MatrixValue(std::move(result));
            }
            std::vector<double> indices;
            for(std::size_t linear=0;linear<input.rows*input.columns;++linear){tick(span);const auto storage=((linear%input.rows)*input.columns+linear/input.rows)*input.components;bool nonzero=false;for(std::size_t c=0;c<input.components;++c)nonzero=nonzero||input.values[storage+c]!=0;if(nonzero)indices.push_back(static_cast<double>(linear+1));}
            return numericResult(indices,indices.size(),1,1,span);
        }
        if(name=="dot"||name=="norm") {
            const auto input=numericArray(arguments.at(0),span);if(input.components!=1)fail("R1950","dot/norm use real arrays; select Pentarnion coefficients explicitly.",span);
            uint32_t operation=PENTA_REDUCE_NORM;std::vector<double> other;
            if(name=="dot"){
                if(input.rows>1&&input.columns>1)fail("R1950","dot requires real vectors.",span);
                other=numericVector(arguments.at(1),span);
                if(other.size()!=input.values.size())fail("R1950","dot vector lengths must match.",span);
                operation=PENTA_REDUCE_DOT;
            }else{
                const bool vector=input.rows<=1||input.columns<=1;bool frobenius=false;
                if(arguments.size()>1){if(const auto* text=std::get_if<std::string>(&arguments[1])){if(*text=="inf")operation=PENTA_REDUCE_NORM_INFINITY;else if(*text=="fro")frobenius=true;else fail("R1950","norm accepts 1, 2, \"inf\" or \"fro\".",span);}else{const double p=real(arguments[1],span);if(p==1)operation=PENTA_REDUCE_NORM_L1;else if(p!=2)fail("R1950","norm accepts 1, 2, \"inf\" or \"fro\".",span);}}
                if(!vector&&!frobenius)fail("R1950","Matrix norm currently requires \"fro\"; default/1/2/inf modes are vector norms.",span);
            }
            double result=0;penta_numeric_reduce_request_v1 request{};request.struct_size=sizeof(request);request.operation=operation;request.width=1;request.count=input.values.size();request.a=input.values.data();request.b=other.data();request.output=&result;request.output_capacity=1;
            chargeNumericWork(input.values.size(),span);
            try{NumericModule(options_.numericModuleDirectory).reduce(request,parallel_.settings,options_.cancellation,parallel_.statistics);}catch(const NumericModuleError& error){fail(error.status==PENTA_NUMERIC_CANCELLED?"R1006":"R1950",error.what(),span);}return finite(result,span);
        }
        if(name=="interp1"||name=="deval")return numericalInterpolation(expression,arguments);
        if(name=="trapz"||name=="cumtrapz")return numericalTrapezoid(expression,arguments);
        if(name=="diff"||name=="gradient"||name=="divergence"||name=="laplacian")return numericalDerivatives(expression,arguments);
        if(name=="integral")return numericalIntegral(expression,arguments);
        return {};
    }
    Value numericalInterpolation(const Expr& expression,const std::vector<Value>& arguments) {
        const auto span=expression.span;const bool dense=expression.text=="deval";
        NumericArray query=numericArray(arguments.at(dense?1:2),span);
        if(query.components!=1)fail("R1950","Interpolation queries must be real values.",span);
        penta_numeric_calculus_request_v1 request{};request.axis=0;request.columns=1;
        request.query=query.values.data();request.query_count=query.values.size();
        NumericArray samples;std::vector<double> coordinates;std::size_t components=1;
        if(dense){
            const auto* stored=std::get_if<SolutionValue>(&arguments[0]);if(!stored)fail("R1950","deval requires an ODE solution.",span);
            const auto& solution=*stored->data;std::size_t selected=0,offset=0;
            if(arguments.size()>2){const auto& state=std::get<std::string>(arguments[2]);const auto found=std::find(solution.stateNames.begin(),solution.stateNames.end(),state);if(found==solution.stateNames.end())fail("R1950","Unknown deval solution state: "+state,span);selected=static_cast<std::size_t>(found-solution.stateNames.begin());}
            else if(solution.stateNames.size()!=1)fail("R1950","deval requires a state name when a solution has several states.",span);
            for(std::size_t n=0;n<selected;++n)offset+=solution.stateWidths[n];
            components=solution.stateWidths.at(selected);
            if(solution.times.empty()||solution.derivatives.size()!=solution.values.size()||offset>=solution.coordinateCount())fail("R1950","This solution has no retained endpoint slopes for cubic Hermite dense evaluation; use explicit interp1 sampling instead.",span);
            request.operation=PENTA_CALC_INTERP_HERMITE;request.rows=solution.times.size();request.value_stride=solution.coordinateCount();request.components=static_cast<uint32_t>(components);
            request.data=solution.values.data()+offset;request.data_capacity=solution.values.size()-offset;
            request.derivative_data=solution.derivatives.data()+offset;request.derivative_capacity=solution.derivatives.size()-offset;
            request.coordinates=solution.times.data();request.coordinate_count=solution.times.size();
        }else{
            coordinates=numericVector(arguments.at(0),span);samples=numericArray(arguments.at(1),span);components=samples.components;
            if((samples.rows>1&&samples.columns>1)||samples.rows*samples.columns!=coordinates.size())fail("R1950","interp1 sample values must be a vector matching the coordinate vector.",span);
            if(arguments.size()>3&&std::get<std::string>(arguments[3])!="linear")fail("R1950","interp1 currently supports the explicit linear method.",span);
            if(arguments.size()>4){const auto& mode=std::get<std::string>(arguments[4]);if(mode!="error"&&mode!="extrap")fail("R1950","interp1 boundary mode must be \"error\" or \"extrap\".",span);request.extrapolate=mode=="extrap";}
            request.operation=PENTA_CALC_INTERP_LINEAR;request.rows=coordinates.size();request.value_stride=components;request.components=static_cast<uint32_t>(components);
            request.data=samples.values.data();request.data_capacity=samples.values.size();request.coordinates=coordinates.data();request.coordinate_count=coordinates.size();
        }
        if(query.rows*query.columns>std::min<std::size_t>(1000000,options_.maxMatrixElements))fail("R1902","Interpolation output exceeds the configured element budget.",span);
        std::vector<double> result(query.values.size()*components);
        std::size_t search=1;for(auto n=request.rows;n>1;n/=2)++search;
        runCalculus(request,result,span,result.size()*search);
        return numericResult(result,query.rows,query.columns,components,span,query.scalar);
    }
    Value numericalTrapezoid(const Expr& expression,const std::vector<Value>& arguments) {
        const auto span=expression.span;const bool cumulative=expression.text=="cumtrapz";
        const bool explicitCoordinates=arguments.size()>1;
        const auto input=numericArray(arguments.at(explicitCoordinates?1:0),span);
        const bool explicitAxis=arguments.size()>2;
        if(!explicitAxis&&input.rows>1&&input.columns>1)fail("R1950","Matrix trapezoidal integration requires an explicit coordinate vector and dimension.",span);
        const auto axis=explicitAxis?integer(arguments[2],span)-1:(input.rows==1?1:0);
        if(axis<0||axis>1)fail("R1950","Integration dimension must be 1 or 2.",span);
        const auto extent=axis==0?input.rows:input.columns;
        auto coordinates=explicitCoordinates?numericVector(arguments[0],span):numericCoordinates(nullptr,extent,span);
        if(coordinates.size()!=extent)fail("R1950","Integration coordinates must match the selected dimension.",span);
        const auto rows=cumulative?input.rows:axis==0?1:input.rows;
        const auto columns=cumulative?input.columns:axis==0?input.columns:1;
        std::vector<double> result(rows*columns*input.components);
        penta_numeric_calculus_request_v1 request{};request.operation=cumulative?PENTA_CALC_CUMTRAPZ:PENTA_CALC_TRAPZ;
        request.rows=input.rows;request.columns=input.columns;request.components=static_cast<uint32_t>(input.components);request.value_stride=input.components;request.axis=static_cast<uint32_t>(axis);
        request.data=input.values.data();request.data_capacity=input.values.size();request.coordinates=coordinates.data();request.coordinate_count=coordinates.size();
        runCalculus(request,result,span,input.values.size());
        if(!cumulative&&!explicitAxis){if(result.empty())result.assign(input.components,0);return numericResult(result,1,1,input.components,span,true);}
        return numericResult(result,rows,columns,input.components,span);
    }
    Value numericalDerivatives(const Expr& expression,const std::vector<Value>& arguments) {
        const auto span=expression.span;const auto& name=expression.text;const auto input=numericArray(arguments.at(0),span);
        if(name=="diff"){
            const I order=arguments.size()>1?integer(arguments[1],span):1;if(order<0||order>100000)fail("R1950","diff order must be an integer in 0..100000.",span);
            const I axis=arguments.size()>2?integer(arguments[2],span)-1:(input.rows==1?1:0);if(axis<0||axis>1)fail("R1950","diff dimension must be 1 or 2.",span);
            const auto extent=axis==0?input.rows:input.columns;const auto length=extent-std::min<std::size_t>(static_cast<std::size_t>(order),extent);
            const auto rows=axis==0?length:input.rows,columns=axis==0?input.columns:length;
            std::vector<double> result(rows*columns*input.components);penta_numeric_calculus_request_v1 request{};
            request.operation=PENTA_CALC_DIFF;request.rows=input.rows;request.columns=input.columns;request.components=static_cast<uint32_t>(input.components);request.axis=static_cast<uint32_t>(axis);request.value_stride=input.components;
            request.data=input.values.data();request.data_capacity=input.values.size();request.difference_order=static_cast<uint32_t>(order);
            const auto cost=input.values.size()*std::max<std::size_t>(1,std::min<std::size_t>(static_cast<std::size_t>(order),extent));
            runCalculus(request,result,span,cost);return numericResult(result,rows,columns,input.components,span);
        }
        if(name=="gradient"||name=="laplacian"){
            const Value* x=arguments.size()>1?&arguments[1]:nullptr;const Value* y=arguments.size()>2?&arguments[2]:x;
            const bool vector=input.rows<=1||input.columns<=1;const auto xAxis=vector?(input.rows==1?1u:0u):1u;
            if(vector&&arguments.size()>2)fail("R1950","A vector gradient/laplacian accepts one spacing or coordinate vector.",span);
            const auto gx=differentiateNumeric(input,xAxis,x,name=="laplacian"?2:1,span);
            NumericArray gy=input;gy.logical=false;gy.values.assign(input.values.size(),0);
            if(!vector)gy=differentiateNumeric(input,0,y,name=="laplacian"?2:1,span);
            if(name=="gradient")return makeTuple({numericResult(gx.values,gx.rows,gx.columns,gx.components,span),numericResult(gy.values,gy.rows,gy.columns,gy.components,span)},span);
            std::vector<double> sum(gx.values.size());for(std::size_t n=0;n<sum.size();++n){tick(span);sum[n]=finite(static_cast<double>(static_cast<long double>(gx.values[n])+gy.values[n]),span);}
            return numericResult(sum,input.rows,input.columns,input.components,span);
        }
        const auto other=numericArray(arguments.at(1),span);
        if(other.rows!=input.rows||other.columns!=input.columns||other.components!=input.components)fail("R1950","divergence fields must have equal shapes and element types.",span);
        const Value* x=arguments.size()>2?&arguments[2]:nullptr;const Value* y=arguments.size()>3?&arguments[3]:x;
        const auto dx=differentiateNumeric(input,1,x,1,span),dy=differentiateNumeric(other,0,y,1,span);
        std::vector<double> sum(dx.values.size());for(std::size_t n=0;n<sum.size();++n){tick(span);sum[n]=finite(static_cast<double>(static_cast<long double>(dx.values[n])+dy.values[n]),span);}
        return numericResult(sum,input.rows,input.columns,input.components,span);
    }
    Value numericalIntegral(const Expr& expression,const std::vector<Value>& arguments) {
        const auto span=expression.span;double a=real(arguments.at(1),span),b=real(arguments.at(2),span);
        double absolute=1e-9,relative=1e-8;std::size_t maximum=10000;
        if(arguments.size()>3){const auto* fields=recordFields(arguments[3]);if(!fields)fail("R1950","integral options must be a record.",span);
            for(const auto& [key,value]:*fields){if(key=="abs_tol")absolute=real(value,span);else if(key=="rel_tol")relative=real(value,span);else if(key=="max_intervals"){const auto n=integer(value,span);if(n<1||n>100000)fail("R1950","integral max_intervals must be in 1..100000.",span);maximum=static_cast<std::size_t>(n);}else fail("R1950","Unknown integral option: "+key,span);}}
        if(absolute<=0||relative<0)fail("R1950","integral tolerances require abs_tol>0 and rel_tol>=0.",span);
        const bool reverse=a>b;if(reverse)std::swap(a,b);
        const Value firstValue=invokeCallable(arguments[0],{Value(a)},span);const bool pentarnion=std::holds_alternative<P>(firstValue);const auto first=element(firstValue,span);
        struct Callback {Evaluator* evaluator;const Value* function;SourceSpan span;std::optional<Failure> failure;};
        Callback callback{this,&arguments[0],span,{}};const double endpoints[]{a,b};std::vector<double> result(pentarnion?5:1);
        penta_numeric_calculus_request_v1 request{};request.struct_size=sizeof(request);request.operation=PENTA_CALC_QUADRATURE;
        request.components=static_cast<uint32_t>(result.size());request.data=first.coefficients().data();request.data_capacity=first.coefficients().size();request.coordinates=endpoints;request.coordinate_count=2;
        request.output=result.data();request.output_capacity=result.size();request.absolute_tolerance=absolute;request.relative_tolerance=relative;request.maximum_intervals=maximum;request.integrand_context=&callback;
        request.integrand=[](void* raw,double coordinate,double* output,uint32_t components,char* error,uint64_t capacity)->int {
            auto& call=*static_cast<Callback*>(raw);
            try{const Value value=call.evaluator->invokeCallable(*call.function,{Value(coordinate)},call.span);const auto entry=call.evaluator->element(value,call.span);
                if(components==1&&std::holds_alternative<P>(value))fail("R1950","Quadrature integrand changed its result element type.",call.span);
                for(uint32_t c=0;c<components;++c)output[c]=entry.coefficients()[c];
                return PENTA_NUMERIC_OK;
            }catch(const Failure& failure){
                try{call.failure.emplace(failure.code,failure.what(),failure.span);}catch(...){return PENTA_NUMERIC_INTERNAL;}
                if(error&&capacity){const auto length=std::min<std::size_t>(std::char_traits<char>::length(failure.what()),static_cast<std::size_t>(capacity-1));std::copy_n(failure.what(),length,error);error[length]=0;}
                return failure.code=="R1006"?PENTA_NUMERIC_CANCELLED:PENTA_NUMERIC_DOMAIN;
            }catch(...){return PENTA_NUMERIC_INTERNAL;}
        };
        try{NumericModule(options_.numericModuleDirectory).calculus(request,parallel_.settings,options_.cancellation,parallel_.statistics);}
        catch(const NumericModuleError& error){if(callback.failure)throw *callback.failure;fail(error.status==PENTA_NUMERIC_CANCELLED?"R1006":"R1950",error.what(),span);}
        if(reverse)for(auto& value:result)value=-value;
        return numericResult(result,1,1,result.size(),span,true);
    }
