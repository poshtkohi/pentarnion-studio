    using PentSymbolFields=std::map<std::string,Value>;
    std::string pentSymbolText(const Value& value,SourceSpan span){const auto* string=std::get_if<std::string>(&value);if(!string)fail("R2160","Exact symbolic values/options require strings.",span);return *string;}
    const PentSymbolFields& pentSymbolFields(const Value& value,SourceSpan span){
        const auto* fields=recordFields(value);if(!fields||fields->size()!=12||!fields->contains("format")||pentSymbolText(fields->at("format"),span)!="PentarnionExactCoefficients-v1")fail("R2160","Expected an exact coefficient record from symexpand().",span);
        for(const auto* key:{"expression","source_expression","gamma","reals","pentarnions","domain","c0","c1","c2","c3","c4"}){if(!fields->contains(key))fail("R2160","Incomplete exact symbolic record.",span);(void)pentSymbolText(fields->at(key),span);}return *fields;
    }
    std::optional<Value> pentSymbolicCall(const Expr& expression,const std::vector<Value>& arguments){
        if(auto value=casCall(expression,arguments))return value;
        const auto& name=expression.text;const auto span=expression.span;
        static const std::set<std::string> names{"symexpand","symcoeff","sympentdiff","sympentsubs","sympentnumeric","sympenttext","sympentcode"};if(!names.contains(name))return {};
        try{
            if(arguments.empty())fail("R2160","Exact symbolic commands require an expression or record.",span);
            std::string input,original,reals,pentarnions,gamma,variable,replacement;std::map<std::string,double> bindings;uint32_t operation=PENTA_PENT_EXPAND;
            if(name=="symexpand"){
                if(arguments.size()>2)fail("R2160","Use symexpand(expression[,options(reals=...,pentarnions=...,gamma=...)]).",span);
                input=original=pentSymbolText(arguments[0],span);
                if(arguments.size()==2){const auto* settings=recordFields(arguments[1]);if(!settings)fail("R2160","Exact symbolic declarations require typed options.",span);for(const auto& [key,value]:*settings){if(key=="reals")reals=pentSymbolText(value,span);else if(key=="pentarnions")pentarnions=pentSymbolText(value,span);else if(key=="gamma")gamma=pentSymbolText(value,span);else fail("R2160","Unknown exact symbolic option: "+key,span);}}
                if(gamma.empty())for(unsigned denominator=1;denominator<=1000;++denominator){const auto numerator=static_cast<unsigned>(std::llround(context_.gamma()*denominator));if(static_cast<double>(numerator)/denominator==context_.gamma()){gamma=std::to_string(numerator)+"/"+std::to_string(denominator);break;}}
                if(gamma.empty())fail("R2160","Supply an exact rational gamma string for this workspace, for example options(gamma=\"1/3\").",span);
            }else{
                const auto& fields=pentSymbolFields(arguments[0],span);input=pentSymbolText(fields.at("expression"),span);original=pentSymbolText(fields.at("source_expression"),span);gamma=pentSymbolText(fields.at("gamma"),span);reals=pentSymbolText(fields.at("reals"),span);pentarnions=pentSymbolText(fields.at("pentarnions"),span);
                if(name=="symcoeff"){if(arguments.size()!=2)fail("R2160","Use symcoeff(record,index0to4).",span);const auto index=integer(arguments[1],span);if(index<0||index>4)fail("R2160","Coefficient index must lie in0..4.",span);return fields.at("c"+std::to_string(index));}
                if(name=="sympenttext"){if(arguments.size()!=1)fail("R2160","Use sympenttext(record).",span);return input;}
                if(name=="sympentdiff"){if(arguments.size()!=2)fail("R2160","Use sympentdiff(record,real_coordinate_name).",span);operation=PENTA_PENT_DIFF;variable=pentSymbolText(arguments[1],span);}
                else if(name=="sympentsubs"){if(arguments.size()!=3)fail("R2160","Use sympentsubs(record,real_coordinate_name,real_expression).",span);operation=PENTA_PENT_SUBS;variable=pentSymbolText(arguments[1],span);replacement=pentSymbolText(arguments[2],span);}
                else if(name=="sympentcode"){if(arguments.size()>2)fail("R2160","Use sympentcode(record[,function_name]).",span);operation=PENTA_PENT_CODE;variable=arguments.size()==2?pentSymbolText(arguments[1],span):"expanded_pentarnion";}
                else {if(arguments.size()>2)fail("R2160","Use sympentnumeric(record[,options(real_coordinate=value,...)]).",span);operation=PENTA_PENT_NUMERIC;if(arguments.size()==2){const auto* values=recordFields(arguments[1]);if(!values)fail("R2160","Numeric bindings require a typed options record.",span);for(const auto& [key,value]:*values)bindings.emplace(key,finite(real(value,span),span));}}
            }
            chargeNumericWork(input.size()+1,span);const auto result=symbolic::PentModule(options_.numericModuleDirectory).execute(operation,input,reals,pentarnions,gamma,variable,replacement,bindings,options_.maxNativeOperations,options_.cancellation,[this,span](std::size_t){chargeNumericWork(1,span);});
            if(operation==PENTA_PENT_NUMERIC){if(result.gamma!=context_.gamma())fail("R2160","The exact symbolic gamma differs from the immutable workspace context; select the matching algebra before numeric conversion.",span);return P(context_,result.coefficients);}
            if(operation==PENTA_PENT_CODE){language::AnalysisOptions validation;validation.gamma=result.gamma;const auto checked=language::analyse(result.text,validation);if(!checked.ok())fail("R2160","The requested generated native function name/parameters do not satisfy the checked language rules.",span);return result.text;}
            std::array<std::string,5> coefficients;std::size_t at=0;for(unsigned c=0;c<5;++c){const auto end=result.text.find('\n',at);if((c<4&&end==std::string::npos)||(c==4&&end!=std::string::npos))fail("R2160","Invalid exact coefficient capability result.",span);coefficients[c]=result.text.substr(at,end==std::string::npos?end:end-at);at=end==std::string::npos?result.text.size():end+1;}
            std::string expanded=coefficients[0];const std::string basis[]{"1","i","j","k","ell"};for(unsigned c=1;c<5;++c)expanded+="+("+coefficients[c]+")*"+basis[c];if(expanded.size()>32768)fail("R1005","Expanded symbolic expression exceeds32768 bytes.",span);
            PentSymbolFields fields{{"format",std::string("PentarnionExactCoefficients-v1")},{"expression",expanded},{"source_expression",original},{"gamma",gamma},{"reals",reals},{"pentarnions",pentarnions},{"domain",std::string("declared_central_real_coefficients; exact_nonnegative_radicals; A_gamma_v1")}};for(unsigned c=0;c<5;++c)fields.emplace("c"+std::to_string(c),coefficients[c]);return makeRecord(std::move(fields),span);
        }catch(const symbolic::Error& error){fail(error.status==PENTA_NUMERIC_CANCELLED?"R1006":error.status==PENTA_PENT_SYMBOLIC_LIMIT?"R1005":"R2160",error.what(),span);}
    }
#include "cas_helpers.inc"
