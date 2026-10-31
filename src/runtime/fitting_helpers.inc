    struct SolverNativeBridge {
        const native_functions::Callback* callback=nullptr;double gamma=1;
        std::vector<double> input;native_functions::DerivativeRequest derivative;
        std::size_t nativeEvaluations=0;std::exception_ptr failure;
        std::vector<double> lower,upper;
        SolverNativeBridge(const native_functions::Callback* cb,double g,std::vector<double> point,native_functions::DerivativeRequest request={},std::size_t count=0,std::exception_ptr exception={}):callback(cb),gamma(g),input(std::move(point)),derivative(std::move(request)),nativeEvaluations(count),failure(std::move(exception)){}
        static void message(char* out,uint64_t capacity,const char* value)noexcept{if(!out||!capacity)return;uint64_t i=0;for(;i+1<capacity&&value[i];++i)out[i]=value[i];out[i]=0;}
        static std::pair<double,double> stencil(double x,double lower,double upper){
            const double step=std::cbrt(std::numeric_limits<double>::epsilon())*(1+std::abs(x));auto valid=[&](double a,double b){return std::isfinite(a)&&std::isfinite(b)&&a!=x&&b!=x&&a!=b&&a>=lower&&a<=upper&&b>=lower&&b<=upper;};
            if(valid(x+step,x-step))return {x+step,x-step};
            for(double direction:{1.,-1.}){const double room=direction>0?upper-x:x-lower;if(!(room>0))continue;const double h=std::isfinite(room)?std::min(step,room/2):step;double a=x+direction*h,b=x+direction*(2*h);if(a==x)a=std::nextafter(x,direction>0?std::numeric_limits<double>::infinity():-std::numeric_limits<double>::infinity());if(b==x||b==a)b=std::nextafter(a,direction>0?std::numeric_limits<double>::infinity():-std::numeric_limits<double>::infinity());if(valid(a,b))return {a,b};}
            throw std::runtime_error("Cannot form two distinct finite-difference probes inside the parameter bounds; use analytic derivatives or wider bounds.");
        }
        static native_functions::DerivativeResult bounded(const native_functions::Callback& callback,double gamma,const std::vector<double>& input,const native_functions::DerivativeRequest& request,const std::vector<double>& lower,const std::vector<double>& upper){
            if(request.order==0||request.method!=native_functions::DerivativeMethod::FiniteDifference)return callback.evaluate(gamma,input,request);
            auto valueRequest=request;valueRequest.order=0;valueRequest.method=native_functions::DerivativeMethod::Automatic;auto result=callback.evaluate(gamma,input,valueRequest);result.columns=request.active.size();result.jacobian.assign(result.values.size()*result.columns,0);std::vector<double> probe=input;
            for(std::size_t c=0;c<request.active.size();++c){const auto index=request.active[c];const auto [first,second]=stencil(input[index],lower.empty()?-std::numeric_limits<double>::infinity():lower[c],upper.empty()?std::numeric_limits<double>::infinity():upper[c]);probe[index]=first;const auto a=callback.evaluate(gamma,probe,valueRequest);probe[index]=second;const auto b=callback.evaluate(gamma,probe,valueRequest);probe[index]=input[index];result.nativeEvaluations+=a.nativeEvaluations+b.nativeEvaluations;const double h1=first-input[index],h2=second-input[index];for(std::size_t row=0;row<result.values.size();++row)result.jacobian[row*result.columns+c]=((h2/h1)*(a.values[row]-result.values[row])-(h1/h2)*(b.values[row]-result.values[row]))/(h2-h1);}
            return result;
        }
        static int evaluate(void* opaque,const double* x,uint64_t n,double* residual,uint64_t m,double* jacobian,char* error,uint64_t capacity)noexcept{
            auto& self=*static_cast<SolverNativeBridge*>(opaque);try{
                std::copy_n(x,static_cast<std::size_t>(n),self.input.begin());self.derivative.order=jacobian?1u:0u;
                const auto result=bounded(*self.callback,self.gamma,self.input,self.derivative,self.lower,self.upper);self.nativeEvaluations+=result.nativeEvaluations;
                if(result.values.size()!=m||(jacobian&&result.jacobian.size()!=m*n))throw std::runtime_error("Compiled numerical callback returned an inconsistent residual/Jacobian layout.");
                std::copy(result.values.begin(),result.values.end(),residual);if(jacobian)std::copy(result.jacobian.begin(),result.jacobian.end(),jacobian);return 0;
            }catch(const std::exception& value){self.failure=std::current_exception();message(error,capacity,value.what());return 7;}catch(...){self.failure=std::current_exception();message(error,capacity,"Unknown native numerical callback failure.");return 7;}
        }
    };
    Value solverPointResult(const std::vector<double>& coordinates,const Value& point,const language::FunctionDefinition& definition,SourceSpan span){
        if(!definition.parameters.empty()){const auto parameter=nativeParameter(definition,0,span);if((parameter.type==Type::RealMatrix||parameter.type==Type::PentarnionMatrix||parameter.type==Type::ElementMatrix||parameter.type==Type::Tensor)&&!std::holds_alternative<TupleValue>(point))return nativeCoordinateValue(coordinates,parameter,span);}
        if(const auto* tuple=std::get_if<TupleValue>(&point)){std::vector<Value> values;std::size_t offset=0;for(std::size_t n=0;n<(*tuple)->values.size();++n){const auto parameter=nativeParameter(definition,n,span);const auto width=language::coordinateWidth(parameter.type,parameter.schema);if(offset>coordinates.size()||width>coordinates.size()-offset)fail("N2001","Native solver result has an incomplete typed tuple layout.",span);values.push_back(nativeCoordinateValue(std::vector<double>(coordinates.begin()+static_cast<std::ptrdiff_t>(offset),coordinates.begin()+static_cast<std::ptrdiff_t>(offset+width)),parameter,span));offset+=width;}if(offset!=coordinates.size())fail("N2001","Native solver result has unexpected trailing coordinates.",span);return makeTuple(std::move(values),span);}
        if(std::holds_alternative<MatrixValue>(point)||rangeValue(point)){const auto shape=numericArray(point,span);return numericResult(coordinates,shape.rows,shape.columns,1,span);}
        return nativeCoordinateValue(coordinates,nativeParameter(definition,0,span),span);
    }
    Value fittingReport(const fitting::Result& solved,Value point,const std::string& method,const std::string& loss,const std::string& jacobian,std::size_t nativeEvaluations,bool cacheHit,const std::string& cacheKey,I requestedThreads,I seed,SourceSpan span,std::map<std::string,Value> extra={}){
        const auto& r=solved.report;const auto n=solved.solution.size(),m=solved.residual.size();
        std::map<std::string,Value> fields{{"x",std::move(point)},{"residuals",numericResult(solved.residual,m,1,1,span)},{"jacobian",r.jacobian_available?numericResult(solved.jacobian,m,n,1,span):numericResult({},0,0,1,span)},{"jacobian_available",I(r.jacobian_available)},
            {"covariance",r.covariance_valid?numericResult(solved.covariance,n,n,1,span):numericResult({},0,0,1,span)},
            {"covariance_valid",I(r.covariance_valid)},{"covariance_assumption",std::string(r.covariance_assumption)},{"noise_variance",r.noise_variance},
            {"cost",r.cost},{"initial_cost",r.initial_cost},{"residual_norm",r.residual_norm},{"weighted_residual_norm",r.weighted_residual_norm},{"gradient_norm",r.gradient_norm},{"rcond",r.rcond},{"rank",I(r.rank)},
            {"iterations",I(r.iterations)},{"evaluations",I(r.evaluations)},{"jacobian_evaluations",I(r.jacobian_evaluations)},{"native_evaluations",I(nativeEvaluations)},{"starts_completed",I(r.starts_completed)},{"best_start",I(r.best_start)},
            {"converged",I(r.converged)},{"status",std::string(r.status)},{"message",std::string(r.message)},{"flag",I(r.backend_flag)},{"history",numericResult(solved.history,r.history_rows,5,1,span)},
            {"elapsed_seconds",r.elapsed_seconds},{"method",method},{"loss",loss},{"jacobian_method",jacobian},{"backend",std::string("ceres")},{"cache_hit",I(cacheHit)},{"cache_key",cacheKey},{"threads_requested",requestedThreads},{"threads_used",I(r.threads_used)},{"backend_threads",I(r.threads)},{"seed",seed}};
        fields.insert(std::make_move_iterator(extra.begin()),std::make_move_iterator(extra.end()));return makeRecord(std::move(fields),span);
    }
    struct FittingOptions {
        penta_fit_request_v1 request{};std::string method="lm",loss="none",jacobian="analytic";
        I requestedThreads=0,seed=0;std::vector<double> lower,upper,scale,weights,starts;
    };
    FittingOptions fittingOptions(const std::map<std::string,Value>* fields,const std::vector<double>& initial,std::size_t residuals,SourceSpan span,const std::set<std::string>& extra={}){
        requireOutsideIterationObserver(span);
        const std::set<std::string> allowed{"parameters","method","loss","loss_scale","jacobian","weights","lower","upper","parameter_scale","max_iterations","max_evaluations","max_wall_seconds","ftol","gtol","xtol","covariance","noise_variance","threads","starts","multistart","seed","history_limit","OutputFcn","OutputEvery"};
        if(fields)for(const auto& [key,value]:*fields){(void)value;if(!allowed.contains(key)&&!extra.contains(key))fail("R2020","Unknown fitting option: "+key,span);}
        auto option=[&](const char* key)->const Value*{if(!fields)return nullptr;const auto it=fields->find(key);return it==fields->end()?nullptr:&it->second;};
        auto text=[&](const char* key,const std::string& fallback){const auto* value=option(key);if(!value)return fallback;const auto* result=std::get_if<std::string>(value);if(!result)fail("R2020",std::string(key)+" must be a string.",span);return *result;};
        auto number=[&](const char* key,double fallback){return option(key)?real(*option(key),span):fallback;};
        auto count=[&](const char* key,I fallback,I maximum){const I v=option(key)?integer(*option(key),span):fallback;if(v<1||v>maximum)fail("R2020",std::string(key)+" is outside its positive bounded range.",span);return static_cast<uint64_t>(v);};
        FittingOptions result;auto& q=result.request;q.struct_size=sizeof(q);q.dimension=initial.size();q.residual_count=residuals;q.initial=initial.data();q.method=1;q.loss=0;
        if(initial.empty()||initial.size()>128||residuals<1||residuals>65536||initial.size()*residuals>1048576)fail("R2020","Fitting dimensions exceed128 unknowns,65536 residuals or1048576 Jacobian entries.",span);
        if(initial.size()*residuals>options_.maxMatrixElements)fail("R2020","The returned residual Jacobian exceeds the configured per-matrix element budget.",span);
        result.method=text("method","lm");if(result.method=="levenberg_marquardt")result.method="lm";if(result.method!="lm"&&result.method!="dogleg")fail("R2020","Fitting method must be lm or dogleg.",span);q.method=result.method=="lm"?1:2;
        result.loss=text("loss","none");if(result.loss=="linear")result.loss="none";if(result.loss=="huber")q.loss=1;else if(result.loss=="cauchy")q.loss=2;else if(result.loss=="soft_l1")q.loss=3;else if(result.loss!="none")fail("R2020","Loss must be none, huber, cauchy or soft_l1.",span);
        result.jacobian=text("jacobian","analytic");if(result.jacobian!="analytic"&&result.jacobian!="ad"&&result.jacobian!="automatic"&&result.jacobian!="finite_difference")fail("R2020","Fitting Jacobian must be analytic/ad/automatic or finite_difference of native callbacks.",span);
        result.requestedThreads=option("threads")?integer(*option("threads"),span):static_cast<I>(parallel_.settings.threads);if(result.requestedThreads<0||result.requestedThreads>64)fail("R2020","Fitting threads must be in0..64.",span);q.threads=static_cast<uint32_t>(result.requestedThreads);
        result.seed=option("seed")?integer(*option("seed"),span):0;if(result.seed<0||result.seed>4294967295LL)fail("R2020","Fitting seed requires a32-bit nonnegative Integer.",span);
        q.max_iterations=count("max_iterations",200,1000000);q.max_evaluations=count("max_evaluations",10000,10000000);const auto historyMaximum=std::min<uint64_t>(20000,options_.maxMatrixElements/5);if(option("history_limit")){const I limit=integer(*option("history_limit"),span);if(limit<0||static_cast<uint64_t>(limit)>historyMaximum)fail("R2020","history_limit exceeds the configured five-column matrix budget.",span);q.history_capacity=static_cast<uint64_t>(limit);}else q.history_capacity=std::min(historyMaximum,q.max_iterations+1);
        q.function_tolerance=number("ftol",1e-10);q.gradient_tolerance=number("gtol",1e-10);q.parameter_tolerance=number("xtol",1e-10);q.maximum_wall_seconds=number("max_wall_seconds",60);q.loss_scale=number("loss_scale",1);q.noise_variance=number("noise_variance",-1);
        const I covariance=option("covariance")?integer(*option("covariance"),span):0;if(covariance<0||covariance>1)fail("R2020","covariance must be0 or1.",span);q.covariance=static_cast<uint32_t>(covariance);
        auto vectorOption=[&](const char* key,std::vector<double>& output,std::size_t size){if(const auto* value=option(key)){if(std::string_view(key)=="weights"){const auto data=numericArray(*value,span);if(data.components!=1)fail("R2020","weights must contain real values.",span);output=data.values;}else output=numericVector(*value,span,false);if(output.size()==1&&size>1)output.resize(size,output.front());if(output.size()!=size)fail("R2020",std::string(key)+" must be scalar or match its flattened real-coordinate count.",span);}};
        vectorOption("lower",result.lower,initial.size());vectorOption("upper",result.upper,initial.size());vectorOption("parameter_scale",result.scale,initial.size());vectorOption("weights",result.weights,residuals);
        result.starts=initial;
        if(const auto* value=option("starts")){if(option("multistart"))fail("R2020","Choose explicit starts or generated multistart, not both.",span);const auto matrix=numericArray(*value,span);if(matrix.components!=1||matrix.columns!=initial.size()||matrix.rows>255)fail("R2020","starts must have one candidate per row and one column per unknown, at most255 extra rows.",span);result.starts.insert(result.starts.end(),matrix.values.begin(),matrix.values.end());}
        if(option("multistart")){const auto starts=count("multistart",1,256);if(starts>1&&(result.lower.empty()||result.upper.empty()))fail("R2020","Generated multistart requires finite lower and upper bounds.",span);std::mt19937_64 generator(static_cast<uint64_t>(result.seed));for(uint64_t row=1;row<starts;++row)for(std::size_t c=0;c<initial.size();++c){const double lo=result.lower[c],hi=result.upper[c];if(!(lo<hi)||!std::isfinite(hi-lo))fail("R2020","Generated multistart bounds require a finite positive width.",span);const double uniform=static_cast<double>(generator()>>11)*0x1.0p-53;result.starts.push_back(lo+(hi-lo)*uniform);}}
        q.start_count=result.starts.size()/initial.size();return result;
    }
    void bindFittingOptions(FittingOptions& options){auto& q=options.request;q.lower=options.lower.empty()?nullptr:options.lower.data();q.upper=options.upper.empty()?nullptr:options.upper.data();q.parameter_scale=options.scale.empty()?nullptr:options.scale.data();q.weights=options.weights.empty()?nullptr:options.weights.data();q.starts=options.starts.data();}
    std::optional<Value> fittingCall(const Expr& expression,const std::vector<Value>& arguments){
        const auto& name=expression.text;const auto span=expression.span;if(name!="lsqfit"&&name!="lsqnonlin")return {};
        requireOutsideIterationObserver(span);
        if(arguments.size()<2||arguments.size()>3)fail("R2020","Use lsqfit(residualFunction,initial[,options]).",span);
        const auto* fields=arguments.size()==3?recordFields(arguments[2]):nullptr;if(arguments.size()==3&&!fields)fail("R2020","Fitting options must be a typed record.",span);
        const auto input=derivativeInput(arguments[0],arguments[1],fields,span);
        try{
            native_functions::Callback callback(*input.definition,derivativeNativeOptions(span));auto options=fittingOptions(fields,std::vector<double>(input.flat.begin(),input.flat.begin()+static_cast<std::ptrdiff_t>(input.variableCoordinates)),callback.outputCount(),span);
            SolverNativeBridge bridge{&callback,context_.gamma(),input.flat,{},0,{}};bridge.lower=options.lower;bridge.upper=options.upper;bridge.derivative.active.resize(input.variableCoordinates);std::iota(bridge.derivative.active.begin(),bridge.derivative.active.end(),0);bridge.derivative.method=options.jacobian=="finite_difference"?native_functions::DerivativeMethod::FiniteDifference:native_functions::DerivativeMethod::Automatic;bridge.derivative.requestedThreads=static_cast<std::size_t>(options.requestedThreads);
            bindFittingOptions(options);options.request.initial=options.starts.data();options.request.evaluate=SolverNativeBridge::evaluate;options.request.callback_context=&bridge;
            fitting::Result result;try{result=fitting::Module(options_.numericModuleDirectory).solve(options.request,options_.cancellation,[this,span](std::size_t count){chargeNumericWork(count,span);},makeIterationObserver(fields,span));}catch(...){if(bridge.failure)std::rethrow_exception(bridge.failure);throw;}
            return fittingReport(result,solverPointResult(result.solution,arguments[1],*input.definition,span),options.method,options.loss,options.jacobian,bridge.nativeEvaluations,callback.cacheHit(),callback.key(),options.requestedThreads,options.seed,span);
        }catch(const fitting::Error& value){fail(value.status==PENTA_FIT_CANCELLED?"R1006":"R2020",value.what(),span);}catch(const ode::OdeError& value){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R2020",value.what(),span);}
    }
