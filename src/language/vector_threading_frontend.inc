    std::optional<Type> checkVectorThreadingCall(const ExprPtr& value) {
        static const std::set<std::string> names{
            "sin","cos","exp","sinh","cosh","sqrt","tan","asin","acos","atan","atan2",
            "tanh","asinh","acosh","atanh","log","log10","log2","abs","floor","ceil","round",
            "conj","scalar","pure","norm2","expm1","log1p","erf","erfc","hypot","sign",
            "sum","mean","min","max","dot","norm","axpy","transpose","reshape","diag",
            "meshgrid","ndgrid","find","logical","interp1","deval","trapz","cumtrapz",
            "diff","gradient","divergence","laplacian"};
        if(!names.contains(value->text)||value->children.empty()||value->children.back()->type!=Type::Record)return {};
        if(value->text=="gradient"&&(value->children.front()->type==Type::Function||value->children.front()->type==Type::OdeModel))return {};
        bool valid=true;
        if(std::any_of(value->children.begin(),value->children.end()-1,[](const ExprPtr& item){return item->type==Type::Record;})){add("E2170","Use one final per-call numerical options record.",value->span);valid=false;}
        if(m_options.languageVersion!="0.2"){add("E2170","Per-call numerical settings require language0.2.",value->span);valid=false;}
        if(hasNamedArguments(value)){add("E2170","Use positional numerical arguments and a final options(threads=n) record.",value->span);valid=false;}
        if(const auto schema=value->children.back()->schema)for(const auto& field:schema->fields){
            if(field.name!="threads"){add("E2170","Unknown per-call numerical option: "+field.name,value->span);valid=false;}
            else if(field.type!=Type::Integer&&field.type!=Type::Invalid){add("E2170","Per-call threads requires Integer type.",value->span);valid=false;}
        }
        if(!valid)return Type::Invalid;
        auto originalChildren=value->children;auto originalNames=value->argumentNames;
        struct Restore {Expr& value;std::vector<ExprPtr> children;std::vector<std::string> names;~Restore(){value.children=std::move(children);value.argumentNames=std::move(names);}} restore{*value,std::move(originalChildren),std::move(originalNames)};
        value->children.pop_back();if(!value->argumentNames.empty())value->argumentNames.pop_back();
        return check(value,true);
    }
