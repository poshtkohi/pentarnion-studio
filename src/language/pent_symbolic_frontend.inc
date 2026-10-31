    std::optional<Type> checkPentSymbolicCall(const ExprPtr& value){
        if(auto result=checkCasCall(value))return result;
        const auto& name=value->text;static const std::set<std::string> names{"symexpand","symcoeff","sympentdiff","sympentsubs","sympentnumeric","sympenttext","sympentcode"};if(!names.contains(name))return {};
        bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2160",text,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Exact Pentarnion coefficient functions require language0.2.");require(!hasNamedArguments(value),"Use positional arguments and explicit typed options.");const auto count=value->children.size();
        const auto expected=name=="symcoeff"||name=="sympentdiff"?2u:name=="sympentsubs"?3u:1u;
        const bool optional=name=="symexpand"||name=="sympentnumeric"||name=="sympentcode";require(count==expected||(optional&&count==expected+1),"Incorrect exact symbolic argument count.");if(!count)return Type::Invalid;
        require(value->children[0]->type==(name=="symexpand"?Type::String:Type::Record),"Use an expression string for symexpand, or its exact coefficient record for other commands.");
        for(std::size_t n=1;n<count;++n)require(value->children[n]->type==(name=="symcoeff"?Type::Integer:(name=="symexpand"||name=="sympentnumeric")?Type::Record:Type::String),"Exact symbolic argument has the wrong type.");
        if(name=="symcoeff"||name=="sympenttext"||name=="sympentcode")return valid?Type::String:Type::Invalid;
        if(name=="sympentnumeric")return valid?Type::Pentarnion:Type::Invalid;
        auto schema=std::make_shared<ValueSchema>();for(const auto* key:{"format","expression","source_expression","gamma","reals","pentarnions","domain","c0","c1","c2","c3","c4"})schema->fields.push_back({key,Type::String,{}});value->schema=schema;return valid?Type::Record:Type::Invalid;
    }
#include "cas_frontend.inc"
