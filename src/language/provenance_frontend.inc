#include "research_archive_frontend.inc"
    std::shared_ptr<ValueSchema> provenanceSchema(){
        auto result=std::make_shared<ValueSchema>(),scientific=std::make_shared<ValueSchema>(),execution=std::make_shared<ValueSchema>();
        for(const auto* name:{"source_hash","import_hashes","native_keys","algebra","language","solver_backend"})scientific->fields.push_back({name,Type::String,{}});
        scientific->fields.push_back({"gamma",Type::Real,{}});for(const auto* name:{"precision_bits","integer_bits"})scientific->fields.push_back({name,Type::Integer,{}});
        for(const auto* name:{"platform","source_directory","cache_directory","dependency_locations"})execution->fields.push_back({name,Type::String,{}});
        for(const auto* name:{"requested_threads","granted_threads","compiler_attempts","cache_hits"})execution->fields.push_back({name,Type::Integer,{}});
        execution->fields.push_back({"compilation_seconds",Type::Real,{}});
        result->fields={{"format",Type::String,{}},{"version",Type::Integer,{}},{"scientific",Type::Record,scientific},{"execution",Type::Record,execution}};return result;
    }
    std::optional<Type> checkProvenanceCall(const ExprPtr& value){
        if(auto archived=checkResearchArchiveCall(value))return archived;
        const auto& name=value->text;if(name!="provenance"&&name!="researchrecord")return {};
        bool valid=true;auto require=[&](bool ok,const char* message){if(!ok){add("E2052",message,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Research provenance requires language0.2.");require(!hasNamedArguments(value),"Use positional provenance arguments.");
        const auto count=value->children.size();if(name=="provenance"){require(count==0,"provenance() takes no arguments.");value->schema=provenanceSchema();}
        else{require(count==2||count==3,"Use researchrecord(name,data_record[,settings_record]).");auto schema=std::make_shared<ValueSchema>();schema->fields={{"format",Type::String,{}},{"version",Type::Integer,{}},{"name",Type::String,{}},{"data_hash",Type::String,{}},{"provenance",Type::Record,provenanceSchema()}};
            if(count>=2){require(value->children[0]->type==Type::String&&value->children[1]->type==Type::Record,"Research records require a name and typed data record.");schema->fields.push_back({"data",Type::Record,value->children[1]->schema});}
            if(count==3)require(value->children[2]->type==Type::Record,"Research settings require a typed record.");
            schema->fields.push_back({"settings",Type::Record,count==3?value->children[2]->schema:std::make_shared<ValueSchema>()});value->schema=std::move(schema);
        }return valid?Type::Record:Type::Invalid;
    }
