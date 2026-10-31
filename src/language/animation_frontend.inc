#include "field_query_frontend.inc"
    std::optional<Type> checkAnimationCall(const ExprPtr& value){
        if(auto query=checkFieldQueryCall(value))return query;
        const auto& name=value->text;if(name!="animatefield"&&name!="animationframe"&&name!="playanimation"&&name!="exportframes")return {};
        bool valid=true;auto require=[&](bool okay,const char* message){if(!okay){add("E2017",message,value->span);valid=false;}};require(m_options.languageVersion=="0.2","Field animation requires language 0.2.");require(!hasNamedArguments(value),"Use positional field data and final typed options.");const auto n=value->children.size();
        if(name=="animationframe"||name=="playanimation"){require(n==1,"Supply one playback argument.");if(n==1)require(value->children[0]->type==(name=="animationframe"?Type::Integer:Type::String),"animationframe requires an Integer; playanimation requires on/off text.");}
        else{const std::size_t required=name=="exportframes"?2:1;require(n==required||n==required+1,"Use animatefield(field[,options]) or exportframes(field,directory[,options]).");if(n)require(value->children[0]->type==Type::Record,"Animation input must be a PDE result record.");if(name=="exportframes"&&n>=2)require(value->children[1]->type==Type::String,"Frame export directory must be a string.");if(n>required)require(value->children.back()->type==Type::Record,"Animation settings require a typed options record.");}
        if(name=="exportframes"){auto schema=std::make_shared<ValueSchema>();for(const auto* field:{"frames","bytes","width","height","dpi"})schema->fields.push_back({field,Type::Integer,{}});for(const auto* field:{"directory","manifest","status"})schema->fields.push_back({field,Type::String,{}});value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;}
        return valid?Type::Void:Type::Invalid;
    }
