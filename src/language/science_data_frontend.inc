#include "science_extended_frontend.inc"
    std::optional<Type> checkScienceDataCall(const ExprPtr& value) {
        if(auto extended=checkScienceExtendedCall(value))return extended;
        const auto& name=value->text;
        static const std::set<std::string> names{"fft","ifft","pwelch","autocorr","findpeaks","variance","stddev","covariance","quantile","histogram","normpdf","normcdf","norminv","bootstrap","sym","symdiff","symsubs","symsimplify","symnumeric","symtext","analysisrecord","saveanalysis","loadanalysis"};
        if(!names.contains(name))return {};
        bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2020",text,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Scientific data/symbolic functions require language0.2.");require(!hasNamedArguments(value),"Use positional arguments and typed option records.");
        const auto count=value->children.size();auto schema=std::make_shared<ValueSchema>();
        auto fields=[&](std::initializer_list<const char*> items,Type type){for(const auto* field:items)schema->fields.push_back({field,type,{}});};
        auto realData=[](Type type){return scalarType(type)||type==Type::RealMatrix||type==Type::LogicalMatrix||rangeType(type)||type==Type::RealSeries;};
        if(name=="analysisrecord") {
            require(count==2||count==3,"Use analysisrecord(name,data_record[,settings_record]).");
            if(count>=2){require(value->children[0]->type==Type::String&&value->children[1]->type==Type::Record,"Analysis requires a name string and typed data record.");schema->fields.push_back({"data",Type::Record,value->children[1]->schema});}
            if(count==3)require(value->children[2]->type==Type::Record,"Analysis settings require a typed record.");
            schema->fields.push_back({"settings",Type::Record,count==3?value->children[2]->schema:std::make_shared<ValueSchema>()});fields({"format","name","algebra","language","backend","data_hash"},Type::String);fields({"version"},Type::Integer);fields({"gamma"},Type::Real);value->schema=schema;return valid?Type::Record:Type::Invalid;
        }
        if(name=="saveanalysis"||name=="loadanalysis") {require(count==2,"Use saveanalysis(path,record) or loadanalysis(path,typed_template_record).");if(count==2){require(value->children[0]->type==Type::String&&value->children[1]->type==Type::Record,"Analysis persistence requires a path and typed record.");value->schema=value->children[1]->schema;}return valid?(name=="saveanalysis"?Type::Integer:Type::Record):Type::Invalid;}
        if(name=="sym"||name=="symdiff"||name=="symsubs"||name=="symsimplify"||name=="symnumeric"||name=="symtext") {
            const auto required=name=="symdiff"?2u:name=="symsubs"?3u:1u;require(count==required||(name=="symnumeric"&&count==2),"Incorrect symbolic builtin argument count.");
            if(count)require(value->children[0]->type==Type::String||value->children[0]->type==Type::Record,"Symbolic input requires an expression string or symbolic record.");
            if(name=="symdiff"||name=="symsubs"){if(count>1)require(value->children[1]->type==Type::String,"Symbolic variable must be an identifier string.");if(count>2)require(value->children[2]->type==Type::String||value->children[2]->type==Type::Record,"Symbolic replacement requires an exact expression string or symbolic record.");}
            if(name=="symnumeric"&&count==2)require(value->children[1]->type==Type::Record,"Numeric symbol bindings require a typed options record.");
            if(name=="symnumeric")return valid?Type::Real:Type::Invalid;
            if(name=="symtext")return valid?Type::String:Type::Invalid;
            fields({"format","expression","domain"},Type::String);fields({"exact"},Type::Integer);value->schema=schema;return valid?Type::Record:Type::Invalid;
        }
        if(name=="fft"||name=="ifft")require(count==1,"Use fft(real_vector) or ifft(split_complex_record).");
        else if(name=="quantile"||name=="histogram")require(count==2,"Use quantile(samples,probabilities) or histogram(samples,edges).");
        else if(name=="normpdf"||name=="normcdf"||name=="norminv")require(count==1||count==3,"Normal functions use(value) or(value,mean,sigma).");
        else require(count==1||count==2,"Science functions use(data[,options_or_normalization]).");
        if(!count)return Type::Invalid;
        require(name=="ifft"?value->children[0]->type==Type::Record:realData(value->children[0]->type),"Science input requires real numeric data; select Pentarnion coefficients explicitly.");
        if(count==2){if(name=="pwelch"||name=="autocorr"||name=="findpeaks"||name=="bootstrap")require(value->children[1]->type==Type::Record,"Science options require a typed record.");else if(name=="variance"||name=="stddev"||name=="covariance")require(value->children[1]->type==Type::Integer,"Normalization must be integer0(sample) or1(population).");else require(realData(value->children[1]->type),"Expected real probabilities or histogram edges.");}
        if(count==3)require(scalarType(value->children[1]->type)&&scalarType(value->children[2]->type),"Normal mean/sigma require real scalars.");
        if(name=="variance"||name=="stddev")return valid?Type::Real:Type::Invalid;
        if(name=="covariance")return valid?Type::RealMatrix:Type::Invalid;
        if(name=="quantile")return valid?(count==2&&scalarType(value->children[1]->type)?Type::Real:Type::RealMatrix):Type::Invalid;
        if(name=="normpdf"||name=="normcdf"||name=="norminv")return valid?(scalarType(value->children[0]->type)?Type::Real:Type::RealMatrix):Type::Invalid;
        if(name=="fft"||name=="ifft"){fields({"real","imag","magnitude","phase"},Type::RealMatrix);fields({"samples"},Type::Integer);fields({"normalization"},Type::String);}
        else if(name=="pwelch"){fields({"frequency","power"},Type::RealMatrix);fields({"fs"},Type::Real);fields({"segment","overlap"},Type::Integer);fields({"normalization"},Type::String);}
        else if(name=="autocorr"){fields({"lags","correlation"},Type::RealMatrix);fields({"normalization"},Type::String);}
        else if(name=="findpeaks"){fields({"values","indices"},Type::RealMatrix);fields({"count"},Type::Integer);}
        else if(name=="histogram"){fields({"counts","edges","centers","density"},Type::RealMatrix);fields({"included","excluded"},Type::Integer);}
        else if(name=="bootstrap"){fields({"estimates"},Type::RealMatrix);fields({"mean","standard_error","lower","upper","confidence"},Type::Real);fields({"replicates","seed"},Type::Integer);fields({"method","statistic"},Type::String);}
        value->schema=schema;return valid?Type::Record:Type::Invalid;
    }
