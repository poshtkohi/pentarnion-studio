    std::size_t savedCallbackBytes_=0,savedCallbacks_=0;
    void accountSavedCallback(std::size_t bytes,SourceSpan span){
        if(!bytes||bytes>65536||savedCallbacks_>=256||bytes>8*1024*1024-savedCallbackBytes_)fail("R2001","Saved callback sources exceed64KiB each,256 callbacks or8MiB per request.",span);
        ++savedCallbacks_;savedCallbackBytes_+=bytes;tick(span);
    }
    std::shared_ptr<const language::FunctionDefinition> checkedSavedCallback(const std::string& source,SourceSpan span){
        language::AnalysisOptions checked;checked.algebraId=result_.analysis.program.algebraId;checked.gamma=context_.gamma();checked.maximumSourceBytes=65536;checked.sourceIdentity="<source>";checked.cancellation=options_.cancellation;
        const auto analysis=language::analyse(source,checked);
        if(!analysis.ok()||analysis.program.statements.size()!=1||analysis.program.statements[0].kind!=language::StatementKind::FunctionDefinition||!analysis.program.statements[0].function)fail("R2001","A saved callback must be one self-contained checked numerical function, without imports or external declarations.",span);
        const auto function=analysis.program.statements[0].function;
        if(!function->captureNames.empty())fail("R2001","Captured or anonymous callback state requires a source/experiment bundle.",span);
        try{native_functions::validateCallback(*function);}catch(const ode::OdeError& error){fail("R2001",std::string("Saved callback is outside the pure native numerical subset: ")+error.what(),span);}
        return function;
    }
    nlohmann::json encodeWorkspaceCallback(const FunctionValue& value,SourceSpan span){
        const auto& function=value.definition;
        // Named handles retain a lookup snapshot of the enclosing function names.
        // No environment pointer is serialized: the isolated recheck below proves
        // this numerical body needs only its own declared parameters and builtins.
        if(!function||!value.builtin.empty()||!function->captureNames.empty()||!function->origin||!function->origin->source||function->span.offset>function->origin->source->size()||function->span.length>function->origin->source->size()-function->span.offset)fail("R2001","Save only self-contained source callbacks; builtin handles and lexical captures require explicit source.",span);
        auto source=function->origin->source->substr(function->span.offset,function->span.length);if(function->native)source="native "+source;accountSavedCallback(source.size(),span);
        const auto verified=checkedSavedCallback(source,span);const auto fingerprint=native_functions::callbackFingerprint(*verified);
        try{if(fingerprint!=native_functions::callbackFingerprint(*function))fail("R2001","The saved callback source differs from its checked numerical body.",span);}catch(const ode::OdeError& error){fail("R2001",error.what(),span);}
        return {{"source",source},{"sha256",ode::detail::sha256(source)},{"fingerprint",fingerprint},{"name",function->name}};
    }
    Value decodeWorkspaceCallback(const nlohmann::json& data,SourceSpan span){
        if(!data.is_object()||data.size()!=4||!data.contains("source")||!data.contains("sha256")||!data.contains("fingerprint")||!data.contains("name")||!data.at("source").is_string()||!data.at("sha256").is_string()||!data.at("fingerprint").is_string()||!data.at("name").is_string())fail("R2001","Invalid saved numerical callback snapshot.",span);
        const auto& source=data.at("source").get_ref<const std::string&>();accountSavedCallback(source.size(),span);if(data.at("sha256")!=ode::detail::sha256(source))fail("R2001","Saved callback source hash mismatch.",span);const auto function=checkedSavedCallback(source,span);
        if(data.at("name")!=function->name||data.at("fingerprint")!=native_functions::callbackFingerprint(*function))fail("R2001","Saved callback signature/body fingerprint differs.",span);
        // Loading only checks source. No native cache, external file, compiler,
        // DLL entry point or numerical callback is invoked until explicit use.
        return FunctionValue{function,{},{}};
    }
