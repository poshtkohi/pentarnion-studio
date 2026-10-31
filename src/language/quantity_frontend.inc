    std::optional<Type> checkQuantityCall(const ExprPtr& value){
        if(value->text=="unitcheck"){
            if(value->children.size()!=2||(value->children[0]->type!=Type::Function&&value->children[0]->type!=Type::OdeModel)||value->children[1]->type!=Type::Record){add("E2521","Use unitcheck(callback_or_ode,options(parameter_name=\"SI_unit\",...)).",value->span);return Type::Invalid;}
            auto schema=std::make_shared<ValueSchema>();schema->fields.push_back({"valid",Type::Integer,{}});for(const auto* field:{"name","dimension_order","derivative_order","status","contract"})schema->fields.push_back({field,Type::String,{}});for(const auto* field:{"result_dimensions","parameter_dimensions","jacobian_dimensions"})schema->fields.push_back({field,Type::RealMatrix,{}});value->schema=schema;return Type::Record;
        }
        const auto& name=value->text;static const std::set<std::string> names{"quantity","unitconvert","unitadd","unitsub","unitmul","unitdiv","unitpow","unitvalues","unitcompatible","unitinfo"};if(!names.contains(name))return {};
        bool valid=true;auto require=[&](bool okay,const char* message){if(!okay){add("E2520",message,value->span);valid=false;}};require(m_options.languageVersion=="0.2","Explicit SI quantities require language0.2.");require(!hasNamedArguments(value),"SI quantity APIs use positional arguments.");const auto count=name=="unitinfo"||name=="unitvalues"?1u:2u;require(value->children.size()==count||(name=="unitpow"&&value->children.size()==3),"Incorrect SI quantity argument count.");if(value->children.size()<count)return Type::Invalid;
        auto field=[](const std::shared_ptr<const ValueSchema>& schema,const char* name)->const SchemaField*{if(schema)for(const auto& item:schema->fields)if(item.name==name)return &item;return nullptr;};
        auto data=[&](const ExprPtr& expr){if(const auto* f=field(expr->schema,"data"))return *f;return SchemaField{"data",Type::RealMatrix,{}};};
        bool extended=name=="unitinfo"||(name=="unitpow"&&value->children.size()==3);for(const auto& child:value->children){extended=extended||field(child->schema,"numerators")!=nullptr;if(child->kind==ExprKind::StringLiteral){const auto& text=child->text;extended=extended||text.find("^(")!=std::string::npos||text.find("degC")!=std::string::npos||text.find("degF")!=std::string::npos||text.find("delta_")!=std::string::npos||text=="Celsius"||text=="Fahrenheit"||text=="K_abs"||text.starts_with("dB");}}
        SchemaField magnitude{"data",Type::RealMatrix,{}};
        if(name=="quantity"){
            const auto type=value->children[0]->type;const bool whole=type==Type::Pentarnion||type==Type::Complex||type==Type::Element||type==Type::PentarnionMatrix||type==Type::ElementMatrix;
            require(whole||scalarType(type)||type==Type::RealMatrix||type==Type::LogicalMatrix||rangeType(type),"Quantity magnitudes require real, complex or complete algebra elements/arrays.");require(value->children[1]->type==Type::String,"SI unit expressions require strings.");if(whole){extended=true;magnitude={"data",type,value->children[0]->schema};}
        }else if(name=="unitinfo"||name=="unitcompatible"){
            for(const auto& child:value->children)require(child->type==Type::Record||child->type==Type::String,"Unit metadata accepts a quantity record or unit string.");
        }else{
            require(value->children[0]->type==Type::Record,"SI arithmetic requires explicit quantity records; no implicit stripping is performed.");magnitude=data(value->children[0]);
            if(name=="unitconvert")require(value->children[1]->type==Type::String,"Target unit requires a string.");
            else if(name=="unitpow"){require(value->children[1]->type==Type::Integer,"Quantity powers require integer numerator/exponent.");if(value->children.size()==3)require(value->children[2]->type==Type::Integer,"Rational quantity powers require an integer denominator.");}
            else if(name!="unitvalues"){
                require(value->children[1]->type==Type::Record,"Both operands require explicit quantity records.");const auto right=data(value->children[1]);const auto a=magnitude.type,b=right.type;
                if(a==Type::Element||a==Type::ElementMatrix||b==Type::Element||b==Type::ElementMatrix){const auto& generic=a==Type::Element||a==Type::ElementMatrix?magnitude:right;const auto& other=a==Type::Element||a==Type::ElementMatrix?right:magnitude;require(other.type==Type::RealMatrix||other.type==Type::Element||other.type==Type::ElementMatrix,"Generic quantity arithmetic cannot mix distinct magnitude domains.");if(other.type==Type::Element||other.type==Type::ElementMatrix)require(generic.schema&&other.schema&&generic.schema->algebra&&other.schema->algebra&&generic.schema->algebra->hash()==other.schema->algebra->hash(),"Quantity arithmetic preserves immutable algebra definitions.");magnitude=generic;if(a==Type::ElementMatrix||b==Type::ElementMatrix)magnitude.type=Type::ElementMatrix;else require(a!=Type::RealMatrix&&b!=Type::RealMatrix,"For scalar algebra quantities, embed a real magnitude in the same algebra explicitly.");}
                else if(a==Type::Complex||b==Type::Complex){require(a==Type::Complex&&b==Type::Complex,"Embed real magnitudes explicitly in complex quantities before mixed arithmetic.");magnitude={"data",Type::Complex,{}};}
                else if(a==Type::PentarnionMatrix||b==Type::PentarnionMatrix||((a==Type::Pentarnion||b==Type::Pentarnion)&&(a==Type::RealMatrix||b==Type::RealMatrix)))magnitude={"data",Type::PentarnionMatrix,{}};
                else if(a==Type::Pentarnion||b==Type::Pentarnion)magnitude={"data",Type::Pentarnion,{}};
            }
        }
        if(name=="unitvalues"){value->schema=magnitude.schema;return valid?magnitude.type:Type::Invalid;}
        if(name=="unitcompatible")return valid?Type::Integer:Type::Invalid;
        auto schema=std::make_shared<ValueSchema>();schema->fields.push_back({"unit",Type::String,{}});schema->fields.push_back({"scale",Type::Real,{}});schema->fields.push_back({"dimensions",Type::RealMatrix,{}});
        if(name=="unitinfo"){for(const auto* key:{"canonical","dimension_order"})schema->fields.push_back({key,Type::String,{}});schema->fields.push_back({"affine_supported",Type::Integer,{}});}
        else{for(const auto* key:{"format","kind"})schema->fields.push_back({key,Type::String,{}});schema->fields.push_back(magnitude);}
        if(extended){for(const auto* key:{"offset","log_base","log_factor"})schema->fields.push_back({key,Type::Real,{}});schema->fields.push_back({"mode",Type::Integer,{}});for(const auto* key:{"numerators","denominators"})schema->fields.push_back({key,Type::RealMatrix,{}});}
        value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
