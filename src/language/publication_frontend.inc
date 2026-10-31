#include "histogram_plot_frontend.inc"
    std::optional<Type> checkPublicationCall(const ExprPtr& value){
        if(auto type=checkHistogramPlotCall(value))return type;
        const auto& name=value->text;
        if(name!="hold"&&name!="legend"&&name!="plotstyle"&&name!="xlim"&&name!="ylim"&&name!="zlim"&&name!="axis"&&name!="linkaxes"&&name!="exportgraphics"&&name!="saveplottemplate"&&name!="loadplottemplate"&&name!="linkcamera")return {};
        bool valid=true;auto require=[&](bool okay,const char* message){if(!okay){add("E2016",message,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Publication plotting requires language 0.2.");require(!hasNamedArguments(value),"Use positional plot arguments and a final typed options record.");const auto count=value->children.size();
        if(name=="legend"){require(count>=1&&count<=257,"legend requires 1..256 string labels with optional options.");for(std::size_t k=0;k<count;++k)require(value->children[k]->type==Type::String||(k+1==count&&value->children[k]->type==Type::Record),"Legend labels must be strings; settings are a final typed options record.");}
        else if(name=="linkcamera"){require(count==1,"Use linkcamera([subplot_ids]); an empty vector unlinks3D cameras.");if(count==1)require(value->children[0]->type==Type::RealMatrix||rangeType(value->children[0]->type),"Camera links require a real subplot ID vector.");}
        else if(name=="exportgraphics"||name=="saveplottemplate"||name=="loadplottemplate"){require(count==1||count==2,"Use exportgraphics(path[,options]).");if(count)require(value->children[0]->type==Type::String,"Export path must be a string.");if(count==2)require(value->children[1]->type==Type::Record,"Export settings require a typed options record.");}
        else if(name=="linkaxes"){require(count==2,"Use linkaxes(subplot_ids,axis_mode).");if(count==2){require(value->children[0]->type==Type::RealMatrix||rangeType(value->children[0]->type),"Linked subplot IDs require a real vector.");require(value->children[1]->type==Type::String,"Linked coordinate selection must be a string.");}}
        else{require(count==1,"Plot property commands require one argument.");if(count==1){const auto type=value->children[0]->type;if(name=="plotstyle")require(type==Type::Record,"plotstyle requires a reusable typed options record.");else if(name=="hold")require(type==Type::String,"Use hold(\"on\") or hold(\"off\").");else require(type==Type::RealMatrix||rangeType(type)||type==Type::String,"Axis limits require a real vector or an automatic/equal mode string.");}}
        return valid?Type::Void:Type::Invalid;
    }
