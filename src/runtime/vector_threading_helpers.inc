    bool vectorThreadingFunction(const std::string& name)const {
        static const std::set<std::string> names{
            "sin","cos","exp","sinh","cosh","sqrt","tan","asin","acos","atan","atan2",
            "tanh","asinh","acosh","atanh","log","log10","log2","abs","floor","ceil","round",
            "conj","scalar","pure","norm2","expm1","log1p","erf","erfc","hypot","sign",
            "sum","mean","min","max","dot","norm","axpy","transpose","reshape","diag",
            "meshgrid","ndgrid","find","logical","interp1","deval","trapz","cumtrapz",
            "diff","gradient","divergence","laplacian"};
        return names.contains(name);
    }
    std::optional<Value> vectorThreadingCall(const Expr& expression,const std::vector<Value>& arguments) {
        if(!vectorThreadingFunction(expression.text)||arguments.empty())return {};
        if(expression.text=="gradient"&&(std::holds_alternative<FunctionValue>(arguments.front())||std::holds_alternative<ModelValue>(arguments.front())))return {};
        if(!std::holds_alternative<RecordValue>(arguments.back())){
            // These previously existing real functions now use the same native
            // entrywise pool and per-call settings as sin/cos, also by default.
            if((expression.text=="expm1"||expression.text=="log1p"||expression.text=="erf"||expression.text=="erfc"||expression.text=="sign")&&std::holds_alternative<MatrixValue>(arguments.front()))return parallelCall(expression,arguments);
            return {};
        }
        const auto span=expression.span;const auto& fields=std::get<RecordValue>(arguments.back())->fields;
        if(std::any_of(arguments.begin(),arguments.end()-1,[](const Value& item){return std::holds_alternative<RecordValue>(item);}))fail("R1951","Use one final per-call numerical options record.",span);
        auto settings=parallel_.settings;
        for(const auto& [name,value]:fields){
            if(name!="threads")fail("R1951","Unknown per-call numerical option: "+name,span);
            const auto requested=integer(value,span);if(requested<0||requested>64)fail("R1951","Per-call threads requires Integer0 (automatic) or1..64.",span);
            settings.threads=static_cast<std::size_t>(requested);
        }
        struct ScopedSettings {ParallelSettings& target;ParallelSettings saved;~ScopedSettings(){target=saved;}} restore{parallel_.settings,parallel_.settings};
        parallel_.settings=settings;
        Expr stripped=expression;
        if(!stripped.children.empty())stripped.children.pop_back();
        if(!stripped.argumentNames.empty())stripped.argumentNames.pop_back();
        std::vector<Value> input(arguments.begin(),arguments.end()-1);
        return call(stripped,input);
    }
