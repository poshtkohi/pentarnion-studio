#include "research_archive_helpers.inc"
    Value researchProvenance(SourceSpan span){
        std::set<std::string> imported;std::string dependencyLocations;
        for(const auto& dependency:result_.analysis.dependencies){if(dependency.source)imported.insert(ode::detail::sha256(*dependency.source));dependencyLocations+=dependency.identity+"\n";}
        std::string importHashes,nativeKeys;for(const auto& hash:imported)importHashes+=hash+"\n";
        {std::lock_guard lock(preparedOrigins_->mutex);for(const auto& key:preparedOrigins_->usedKeys)nativeKeys+=key+"\n";}
        auto scientific=makeRecord({{"source_hash",result_.sourceHash},{"import_hashes",importHashes},{"native_keys",nativeKeys},{"algebra",result_.analysis.program.algebraId},{"gamma",context_.gamma()},{"language",result_.analysis.program.languageVersion},{"solver_backend",std::string(ode::backendIdentity())},{"precision_bits",I(std::numeric_limits<double>::digits)},{"integer_bits",I(sizeof(I)*8)}},span);
#ifdef _WIN32
        const std::string platform="windows";
#elif defined(__linux__)
        const std::string platform="linux";
#else
        const std::string platform="other";
#endif
        auto execution=makeRecord({{"platform",platform},{"source_directory",options_.dataDirectory.string()},{"cache_directory",options_.odeCacheDirectory.string()},{"dependency_locations",dependencyLocations},{"requested_threads",I(parallel_.settings.threads)},{"granted_threads",I(ThreadBudget::effectiveThreads(parallel_.settings.threads))},{"compiler_attempts",I(result_.nativeCompilation.compilerAttempts)},{"cache_hits",I(result_.nativeCompilation.cacheHits)},{"compilation_seconds",result_.nativeCompilation.compilationWallSeconds}},span);
        return makeRecord({{"format",std::string("PentarnionProvenance")},{"version",I(1)},{"scientific",std::move(scientific)},{"execution",std::move(execution)}},span);
    }
    std::optional<Value> provenanceCall(const Expr& expression,const std::vector<Value>& arguments){
        if(auto archived=researchArchiveCall(expression,arguments))return archived;
        const auto& name=expression.text;const auto span=expression.span;if(name!="provenance"&&name!="researchrecord")return {};
        if(name=="provenance"){if(!arguments.empty())fail("R2052","provenance() takes no arguments.",span);return researchProvenance(span);}
        if(arguments.size()<2||arguments.size()>3||!std::holds_alternative<std::string>(arguments[0])||!recordFields(arguments[1])||(arguments.size()==3&&!recordFields(arguments[2])))fail("R2052","Use researchrecord(name,data_record[,settings_record]).",span);
        const auto bytes=encodeWorkspaceValue(arguments[1],span).dump();
        return makeRecord({{"format",std::string("PentarnionResearch")},{"version",I(1)},{"name",std::get<std::string>(arguments[0])},{"data_hash",ode::detail::sha256(bytes)},{"data",arguments[1]},{"settings",arguments.size()==3?arguments[2]:makeRecord({},span)},{"provenance",researchProvenance(span)}},span);
    }
