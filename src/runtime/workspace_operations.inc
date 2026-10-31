#include "workspace_callback_helpers.inc"
    dataio::Options dataOptions() const {
        dataio::Options options;options.maxElements=std::min<std::size_t>(100000,options_.maxMatrixElements);
        options.cancellation=options_.cancellation;return options;
    }
    std::filesystem::path dataPath(const std::string& text,SourceSpan span) const {
        if(text.empty()||text.find('\0')!=std::string::npos)fail("R2001","A data file path must be nonempty and contain no NUL.",span);
        auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
        if(path.is_relative()&&!options_.dataDirectory.empty())path=options_.dataDirectory/path;
        return path;
    }
    std::vector<std::size_t> savedFixedArrayShape(const nlohmann::json& entry,std::size_t rows,std::size_t columns,SourceSpan span) const {
        const auto found=entry.find("fixed_shape");if(found==entry.end())return {};
        if(!found->is_array()||(found->size()!=0&&found->size()!=2))fail("R2001","Invalid saved fixed array shape.",span);
        if(found->empty())return {};
        const auto r=savedSize((*found)[0],span),c=savedSize((*found)[1],span);if(!r||!c||r!=rows||c!=columns)fail("R2001","Saved fixed shape differs from the actual array layout.",span);
        return {r,c};
    }
    nlohmann::json encodeWorkspaceAlgebra(const UniversalDefinition& definition,SourceSpan span) {
        tick(span);if(!definition)fail("R2001","Cannot save an unresolved algebra definition.",span);
        return {{"hash",definition->hash()},{"json",definition->json()},{"builtin",definition->builtinKind()},{"source_json",definition->sourceJson()},{"source_hash",definition->sourceHash()}};
    }
    UniversalDefinition decodeWorkspaceAlgebra(const nlohmann::json& snapshot,SourceSpan span) {
        try{
            if(!snapshot.is_object()||snapshot.size()!=5||!snapshot.at("hash").is_string()||!snapshot.at("json").is_string()||!snapshot.at("builtin").is_string()||!snapshot.at("source_json").is_string()||!snapshot.at("source_hash").is_string())fail("R2001","Invalid frozen algebra snapshot.",span);
            const auto text=snapshot.at("json").get<std::string>(),hash=snapshot.at("hash").get<std::string>(),kind=snapshot.at("builtin").get<std::string>();
            const auto source=snapshot.at("source_json").get<std::string>(),sourceHash=snapshot.at("source_hash").get<std::string>();
            algebra::universal::ImportOptions limits;limits.cancellation=options_.cancellation;
            auto definition=algebra::universal::fromJson(source.empty()?text:source,limits);
            if(hash.size()!=64||definition->hash()!=hash||definition->json()!=text)fail("R2001","Frozen algebra snapshot/hash mismatch.",span);
            if(kind.empty()){if(source.empty()||definition->sourceHash()!=sourceHash)fail("R2001","Saved imported algebra source bytes/hash differ.",span);
        return definition;}
            // A saved builtin hint conveys no privileges: reconstruct only a
            // trusted factory definition whose entire normalized table matches.
            if(kind=="clifford"){
                for(unsigned total=0;total<=6;++total)if((std::size_t(1)<<total)==definition->dimension())for(unsigned p=0;p<=total;++p){tick(span);auto candidate=algebra::universal::builtin(kind,{p,total-p});if(source.empty()&&candidate->hash()==hash&&candidate->json()==text&&candidate->sourceHash()==sourceHash)return candidate;}
            }else {auto candidate=algebra::universal::builtin(kind);if(source.empty()&&candidate->hash()==hash&&candidate->json()==text&&candidate->sourceHash()==sourceHash)return candidate;}
            fail("R2001","Saved builtin provenance does not match the frozen multiplication table.",span);
        }catch(const algebra::universal::Error& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R2001",std::string("Invalid saved algebra: ")+error.what(),span);}
    }
    nlohmann::json encodeWorkspaceValue(const Value& value,SourceSpan span,std::size_t depth=0) {
        tick(span);if(depth>16)fail("R2001","Saved values exceed the nesting limit.",span);
        using Json=nlohmann::json;Json entry;entry["type"]=language::typeName(valueType(value));
        if(const auto* x=std::get_if<I>(&value))entry["value"]=*x;
        else if(const auto* x=std::get_if<double>(&value))entry["value"]=*x;
        else if(const auto* x=std::get_if<P>(&value))entry["value"]=x->coefficients();
        else if(const auto* x=std::get_if<std::string>(&value))entry["value"]=*x;
        else if(const auto* x=std::get_if<std::complex<double>>(&value))entry["value"]={x->real(),x->imag()};
        else if(const auto* x=std::get_if<UniversalDefinition>(&value))entry["value"]=encodeWorkspaceAlgebra(*x,span);
        else if(const auto* x=std::get_if<UniversalElement>(&value)){entry["definition"]=encodeWorkspaceAlgebra(x->definition(),span);entry["value"]=x->coefficients();}
        else if(const auto* x=std::get_if<TensorValue>(&value)){entry["entry_type"]=language::typeName((*x)->entryType);entry["shape"]=(*x)->shape;entry["gamma"]=(*x)->gamma;entry["value"]=(*x)->values;if((*x)->entryType==Type::Element)entry["definition"]=encodeWorkspaceAlgebra((*x)->algebra,span);}
        else if(const auto* x=std::get_if<UniversalMatrix>(&value)){entry["definition"]=encodeWorkspaceAlgebra((*x)->algebra,span);entry["rows"]=(*x)->rows;entry["columns"]=(*x)->columns;entry["fixed_shape"]=(*x)->fixedShape;entry["value"]=(*x)->values;}
        else if(const auto* x=std::get_if<FunctionValue>(&value))entry["value"]=encodeWorkspaceCallback(*x,span);
        else if(const auto* x=std::get_if<TupleValue>(&value)){entry["value"]=Json::array();for(const auto& item:(*x)->values)entry["value"].push_back(encodeWorkspaceValue(item,span,depth+1));}
        else if(const auto* x=std::get_if<RecordValue>(&value)){entry["value"]=Json::object();for(const auto& [name,item]:(*x)->fields)entry["value"][name]=encodeWorkspaceValue(item,span,depth+1);}
        else if(const auto* x=std::get_if<IntegerRange>(&value))entry["value"]={x->start,x->step,x->stop};
        else if(const auto* x=std::get_if<RealRange>(&value))entry["value"]={x->start,x->step,x->stop};
        else if(const auto* x=std::get_if<MatrixValue>(&value)){
            entry["rows"]=(*x)->rows;entry["columns"]=(*x)->columns;entry["value"]=Json::array();
            if(!(*x)->fixedShape.empty())entry["fixed_shape"]=(*x)->fixedShape;
            for(const auto& item:(*x)->values){tick(span);if((*x)->pentarnion)entry["value"].push_back(item.coefficients());else entry["value"].push_back(item.coefficients()[0]);}
        }else if(const auto* x=std::get_if<SolutionValue>(&value)){
            const auto& solution=*x->data;entry["times"]=solution.times;entry["state_names"]=solution.stateNames;
            entry["state_widths"]=solution.stateWidths;entry["value"]=solution.values;
            if(!x->definition||x->definition->states.size()!=solution.stateNames.size())fail("R2001","Solution has no complete typed state metadata.",span);
            entry["state_metadata"]=Json::array();for(std::size_t n=0;n<x->definition->states.size();++n){
                const auto& state=x->definition->states[n];if(state.name!=solution.stateNames[n]||language::coordinateWidth(state.type,state.schema)!=solution.stateWidths[n])fail("R2001","Solution state metadata does not match its real-coordinate layout.",span);
                Json metadata={{"type",language::typeName(state.type)}};
                if(state.type==Type::Tensor){metadata["entry_type"]=language::typeName(state.schema->tensorEntryType);metadata["shape"]=state.schema->shape;if(state.schema->tensorEntryType==Type::Element)metadata["definition"]=encodeWorkspaceAlgebra(state.schema->algebra,span);}
                if(state.type==Type::Element||state.type==Type::ElementMatrix)metadata["definition"]=encodeWorkspaceAlgebra(state.schema->algebra,span);
                if(state.type==Type::RealMatrix||state.type==Type::PentarnionMatrix||state.type==Type::ElementMatrix)metadata["shape"]=state.schema->shape;
                entry["state_metadata"].push_back(std::move(metadata));
            }
            entry["derivatives"]=solution.derivatives;entry["dense_rhs_evaluations"]=solution.stats.denseRhsEvaluations;
            entry["sensitivity_count"]=solution.sensitivityCount;entry["sensitivity_parameters"]=solution.sensitivityParameterIndices;entry["sensitivities"]=solution.sensitivities;
            entry["event_times"]=solution.eventTimes;entry["event_values"]=solution.eventValues;entry["event_indices"]=solution.eventIndices;entry["event_directions"]=solution.eventDirections;entry["terminal_event"]=solution.stats.terminalEvent;
            entry["dense_segments"]=Json::array();for(const auto& segment:solution.denseSegments){tick(span);entry["dense_segments"].push_back({{"first",segment.first},{"last",segment.last},{"center",segment.center},{"degree",segment.degree},{"coefficients",segment.coefficients}});}
            entry["method"]=solution.stats.method;entry["jacobian"]=solution.stats.jacobian;
        }else if(const auto* x=std::get_if<ArraySeries>(&value)){
            const auto width=language::coordinateWidth(x->valueType,x->schema),stride=x->data->coordinateCount();if(!width||x->offset>stride||width>stride-x->offset)fail("R2001","Invalid fixed-array series layout.",span);
            auto selected=std::make_shared<ode::Solution>();selected->times=x->data->times;selected->stateNames={"state"};selected->stateWidths={width};selected->values.reserve(selected->times.size()*width);
            for(std::size_t row=0;row<selected->times.size();++row){tick(span);const auto begin=x->data->values.begin()+row*stride+x->offset;selected->values.insert(selected->values.end(),begin,begin+width);}
            auto definition=std::make_shared<language::OdeDefinition>();definition->name="restored_array_series";definition->states.push_back({"state",x->valueType,{},x->schema});entry["value"]=encodeWorkspaceValue(SolutionValue{selected,definition},span,depth+1);
        }else if(const auto* x=std::get_if<UniversalSeries>(&value)){
            const auto width=x->algebra->dimension(),stride=x->data->coordinateCount();if(x->offset>stride||width>stride-x->offset)fail("R2001","Invalid algebra series layout.",span);
            auto selected=std::make_shared<ode::Solution>();selected->times=x->data->times;selected->stateNames={"state"};selected->stateWidths={width};selected->values.reserve(selected->times.size()*width);
            for(std::size_t row=0;row<selected->times.size();++row){tick(span);const auto begin=x->data->values.begin()+row*stride+x->offset;selected->values.insert(selected->values.end(),begin,begin+width);}
            auto schema=std::make_shared<language::ValueSchema>();schema->algebra=x->algebra;auto definition=std::make_shared<language::OdeDefinition>();definition->name="restored_series";definition->states.push_back({"state",x->complex?Type::Complex:Type::Element,{},schema});
            entry["definition"]=encodeWorkspaceAlgebra(x->algebra,span);entry["complex"]=x->complex;entry["value"]=encodeWorkspaceValue(SolutionValue{selected,definition},span,depth+1);
        }else if(std::holds_alternative<RealSeries>(value)||std::holds_alternative<PentSeries>(value)){
            const auto shape=indexedShape(value,span);entry["type"]=shape.pentarnion?"pentarnionmatrix":"realmatrix";
            entry["rows"]=shape.rows;entry["columns"]=shape.columns;entry["value"]=Json::array();
            for(std::size_t row=0;row<shape.rows;++row){tick(span);const auto item=indexedElement(value,row,0,span);if(shape.pentarnion)entry["value"].push_back(item.coefficients());else entry["value"].push_back(item.coefficients()[0]);}
        }else fail("R2001","save stores numerical data and self-contained pure numerical callbacks. Captured functions and ODE definitions require source files.",span);
        return entry;
    }
    I savedInteger(const nlohmann::json& value,SourceSpan span) const {
        if(!value.is_number_integer()||(value.is_number_unsigned()&&value.get<std::uint64_t>()>static_cast<std::uint64_t>(std::numeric_limits<I>::max())))
            fail("R2001","Saved integer fields must be exact signed 64-bit integers.",span);
        return value.get<I>();
    }
    std::size_t savedSize(const nlohmann::json& value,SourceSpan span) const {
        if(!value.is_number_integer()||(!value.is_number_unsigned()&&value.get<I>()<0))
            fail("R2001","Saved dimensions must be exact nonnegative integers.",span);
        const auto result=value.get<std::uint64_t>();
        if(result>std::numeric_limits<std::size_t>::max())fail("R2001","A saved dimension exceeds the platform size limit.",span);
        return static_cast<std::size_t>(result);
    }
    Value decodeWorkspaceValue(const nlohmann::json& entry,SourceSpan span,std::size_t depth=0) {
        tick(span);if(depth>16)fail("R2001","Saved values exceed the nesting limit.",span);
        const auto type=entry.at("type").get<std::string>();const auto& data=entry.at("value");
        auto number=[span](const nlohmann::json& value){if(!value.is_number())fail("R2001","Saved data contains a nonnumeric value.",span);
        return finite(value.get<double>(),span);};
        auto coefficients=[&](const nlohmann::json& value){if(!value.is_array()||value.size()!=5)fail("R2001","Invalid saved Pentarnion coefficient count.",span);
            algebra::Coefficients c{};for(std::size_t n=0;n<5;++n)c[n]=number(value[n]);return P(context_,c);};
        if(type=="function"||type=="callback")return decodeWorkspaceCallback(data,span);
        if(type=="integer")return savedInteger(data,span);
        if(type=="real")return number(data);
        if(type=="complex"){if(!data.is_array()||data.size()!=2)fail("R2001","Invalid saved complex value.",span);
        return std::complex<double>{number(data[0]),number(data[1])};}
        if(type=="algebra")return decodeWorkspaceAlgebra(data,span);
        if(type=="element"){
            auto definition=decodeWorkspaceAlgebra(entry.at("definition"),span);if(!data.is_array()||data.size()!=definition->dimension())fail("R2001","Saved element coefficients do not match its frozen algebra.",span);
            std::vector<double> values;for(const auto& item:data)values.push_back(number(item));return UniversalElement(std::move(definition),std::move(values));
        }
        if(type=="tensor"){
            const auto& dimensions=entry.at("shape");if(!dimensions.is_array()||dimensions.empty()||dimensions.size()>8)fail("R2001","Saved tensor rank must be1..8.",span);std::vector<std::size_t> shape;for(const auto& item:dimensions)shape.push_back(savedSize(item,span));
            const auto kind=entry.at("entry_type").get<std::string>();const auto entryType=kind=="real"?Type::Real:kind=="complex"?Type::Complex:kind=="pentarnion"?Type::Pentarnion:kind=="element"?Type::Element:Type::Invalid;
            if(entryType==Type::Invalid)fail("R2001","Saved tensor entry kind is unsupported.",span);
            const auto definition=entryType==Type::Element?decodeWorkspaceAlgebra(entry.at("definition"),span):UniversalDefinition{};
            if(entryType!=Type::Element&&entry.contains("definition"))fail("R2001","Saved real/complex/Pentarnion tensors cannot carry a generic algebra definition.",span);
            const auto gamma=number(entry.at("gamma"));if(entryType==Type::Pentarnion&&gamma!=context_.gamma())fail("R2001","Saved tensor Pentarnion context differs from the current workspace.",span);
            const auto width=entryType==Type::Element?definition->dimension():entryType==Type::Pentarnion?5u:entryType==Type::Complex?2u:1u;const auto count=checkedTensorCount(shape,width,span);
            if(!data.is_array()||data.size()!=count*width)fail("R2001","Saved tensor coordinates differ from their full typed shape.",span);
            std::vector<double> values;values.reserve(data.size());for(const auto& item:data){tick(span);values.push_back(number(item));}return tensorValue(entryType,definition,gamma,std::move(shape),std::move(values),span);
        }
        if(type=="elementmatrix"){
            auto definition=decodeWorkspaceAlgebra(entry.at("definition"),span);const auto rows=savedSize(entry.at("rows"),span),columns=savedSize(entry.at("columns"),span);auto matrix=newUniversalMatrix(definition,rows,columns,span);
            if(!data.is_array()||data.size()!=matrix->values.size())fail("R2001","Saved native array coordinates differ from its shape/algebra.",span);
        for(std::size_t n=0;n<data.size();++n){tick(span);matrix->values[n]=number(data[n]);}matrix->fixedShape=savedFixedArrayShape(entry,rows,columns,span);return UniversalMatrix(matrix);
        }
        if(type=="array series"){
            const auto restored=decodeWorkspaceValue(data,span,depth+1);const auto* solution=std::get_if<SolutionValue>(&restored);if(!solution||!solution->definition||solution->definition->states.size()!=1)fail("R2001","Invalid saved fixed-array series state metadata.",span);
            const auto& state=solution->definition->states.front();if((state.type!=Type::RealMatrix&&state.type!=Type::PentarnionMatrix&&state.type!=Type::ElementMatrix&&state.type!=Type::Tensor)||language::coordinateWidth(state.type,state.schema)!=solution->data->coordinateCount())fail("R2001","Saved fixed-array series type/shape differs from its coordinate buffer.",span);
        return ArraySeries{solution->data,0,state.type,state.schema};
        }
        if(type=="algebra element series"){
            auto definition=decodeWorkspaceAlgebra(entry.at("definition"),span);if(!entry.at("complex").is_boolean())fail("R2001","Invalid saved algebra series kind.",span);
        const bool complex=entry.at("complex").get<bool>();
            const auto restored=decodeWorkspaceValue(data,span,depth+1);const auto* solution=std::get_if<SolutionValue>(&restored);if(!solution||solution->definition->states.size()!=1||solution->data->stateWidths!=std::vector<std::size_t>{definition->dimension()})fail("R2001","Invalid saved algebra series state layout.",span);
            const auto& state=solution->definition->states.front();if(complex?(state.type!=Type::Complex||definition->hash()!=algebra::universal::builtin("complex")->hash()):(state.type!=Type::Element||!state.schema||!state.schema->algebra||state.schema->algebra->hash()!=definition->hash()))fail("R2001","Saved algebra series type/hash differs from its state metadata.",span);
            return UniversalSeries{solution->data,0,std::move(definition),complex};
        }
        if(type=="tuple"){if(!data.is_array()||data.empty()||data.size()>16)fail("R2001","Invalid saved result tuple.",span);std::vector<Value> values;for(const auto& item:data)values.push_back(decodeWorkspaceValue(item,span,depth+1));
        return makeTuple(std::move(values),span);}
        if(type=="record"||type=="options"){if(!data.is_object()||data.size()>128)fail("R2001","Invalid saved options record.",span);std::map<std::string,Value> fields;for(auto it=data.begin();it!=data.end();++it){if(!validSavedField(it.key()))fail("R2001","Invalid saved record field.",span);fields.emplace(it.key(),decodeWorkspaceValue(it.value(),span,depth+1));}return makeRecord(std::move(fields),span);}
        if(type=="pentarnion")return coefficients(data);
        if(type=="string")return data.get<std::string>();
        if(type=="integer range"){
            if(!data.is_array()||data.size()!=3)fail("R2001","Invalid saved range.",span);
            const auto start=savedInteger(data[0],span),step=savedInteger(data[1],span),stop=savedInteger(data[2],span);if(!step)fail("R2001","Invalid zero saved range step.",span);
        return IntegerRange{start,step,stop};
        }
        if(type=="real range"){
            if(!data.is_array()||data.size()!=3)fail("R2001","Invalid saved range.",span);
            const double start=number(data[0]),step=number(data[1]),stop=number(data[2]);if(!step)fail("R2001","Invalid zero saved range step.",span);
        return RealRange{start,step,stop};
        }
        if(type=="realmatrix"||type=="pentarnionmatrix"||type=="logicalmatrix"){
            const auto rows=savedSize(entry.at("rows"),span),columns=savedSize(entry.at("columns"),span);
            auto matrix=newMatrix(rows,columns,type=="pentarnionmatrix",span);matrix->logical=type=="logicalmatrix";
            if(!data.is_array()||data.size()!=rows*columns)fail("R2001","Saved matrix dimensions do not match its values.",span);
            for(const auto& item:data){tick(span);matrix->values.push_back(matrix->pentarnion?coefficients(item):P::scalar(context_,number(item)));}
            if(matrix->logical)for(const auto& item:matrix->values)if(item.coefficients()[0]!=0&&item.coefficients()[0]!=1)fail("R2001","Saved logical entries must be zero or one.",span);
            matrix->fixedShape=savedFixedArrayShape(entry,rows,columns,span);
            return MatrixValue(std::move(matrix));
        }
        if(type=="solution"){
            auto solution=std::make_shared<ode::Solution>();solution->stateNames=entry.at("state_names").get<std::vector<std::string>>();
            const auto& widths=entry.at("state_widths");if(!widths.is_array()||widths.size()>64)fail("R2001","Invalid saved solution state widths.",span);
            for(const auto& width:widths)solution->stateWidths.push_back(savedSize(width,span));
            if(solution->stateNames.empty()||solution->stateNames.size()>64||solution->stateNames.size()!=solution->stateWidths.size())fail("R2001","Invalid saved solution state schema.",span);
            std::unordered_set<std::string> names;std::size_t width=0;auto definition=std::make_shared<language::OdeDefinition>();definition->name="restored_solution";
            const auto metadata=entry.find("state_metadata");if(metadata!=entry.end()&&(!metadata->is_array()||metadata->size()!=solution->stateNames.size()))fail("R2001","Invalid saved solution state metadata.",span);
            for(std::size_t n=0;n<solution->stateNames.size();++n){const auto& name=solution->stateNames[n];const auto w=solution->stateWidths[n];
                if(!validSavedName(name)||name=="t"||!names.insert(name).second||w<1||w>4096||width>4096-w)fail("R2001","Invalid saved solution state schema.",span);
                Type kind=Type::Invalid;std::shared_ptr<language::ValueSchema> schema;
                if(metadata==entry.end()){if(w!=1&&w!=5)fail("R2001","Legacy saved solutions require one or five coordinates per state.",span);kind=w==5?Type::Pentarnion:Type::Real;}
                else {
                    const auto& item=(*metadata)[n];const auto label=item.at("type").get<std::string>();
                    if(label=="real")kind=Type::Real;else if(label=="pentarnion")kind=Type::Pentarnion;else if(label=="complex")kind=Type::Complex;
                    else if(label=="element"||label=="elementmatrix"){kind=label=="element"?Type::Element:Type::ElementMatrix;schema=std::make_shared<language::ValueSchema>();schema->algebra=decodeWorkspaceAlgebra(item.at("definition"),span);}
                    else if(label=="tensor"){
                        kind=Type::Tensor;schema=std::make_shared<language::ValueSchema>();const auto entryKind=item.at("entry_type").get<std::string>();schema->tensorEntryType=entryKind=="real"?Type::Real:entryKind=="complex"?Type::Complex:entryKind=="pentarnion"?Type::Pentarnion:entryKind=="element"?Type::Element:Type::Invalid;
                        if(schema->tensorEntryType==Type::Invalid)fail("R2001","Saved tensor solution has an invalid explicit entry type.",span);
                        if(schema->tensorEntryType==Type::Element)schema->algebra=decodeWorkspaceAlgebra(item.at("definition"),span);else if(item.contains("definition"))fail("R2001","A nongeneric tensor state cannot carry a generic algebra.",span);
                        const auto& dimensions=item.at("shape");if(!dimensions.is_array()||dimensions.empty()||dimensions.size()>8)fail("R2001","Saved tensor state rank must be1..8.",span);for(const auto& extent:dimensions)schema->shape.push_back(savedSize(extent,span));const auto entryWidth=language::coordinateWidth(schema->tensorEntryType,schema);checkedTensorCount(schema->shape,entryWidth,span);
                    }
                    else if(label=="realmatrix")kind=Type::RealMatrix;else if(label=="pentarnionmatrix")kind=Type::PentarnionMatrix;else fail("R2001","Unsupported saved solution state type.",span);
                    if(kind==Type::RealMatrix||kind==Type::PentarnionMatrix||kind==Type::ElementMatrix){if(!schema)schema=std::make_shared<language::ValueSchema>();const auto& dimensions=item.at("shape");if(!dimensions.is_array()||dimensions.size()!=2)fail("R2001","Saved array state needs exactly two fixed shape dimensions.",span);schema->shape={savedSize(dimensions[0],span),savedSize(dimensions[1],span)};}
                    if(language::coordinateWidth(kind,schema)!=w)fail("R2001","Saved state width differs from its explicit mathematical type.",span);
                }
                width+=w;definition->states.push_back({name,kind,{},schema});}
            const auto& times=entry.at("times");if(!times.is_array()||times.empty()||times.size()>1000000||times.size()>std::min<std::size_t>(8000000,options_.maxSolutionCoordinates)/width||!data.is_array()||data.size()!=times.size()*width)fail("R2001","Saved solution exceeds its coordinate budget or has an invalid layout.",span);
            solution->times.reserve(times.size());solution->values.reserve(data.size());
            int timeDirection=0;for(const auto& item:times){tick(span);const double t=number(item);if(!solution->times.empty()){const int next=t>solution->times.back()?1:t<solution->times.back()?-1:0;if(next==0||(timeDirection&&next!=timeDirection))fail("R2001","Saved solution times must be strictly monotone in one integration direction.",span);timeDirection=next;}solution->times.push_back(t);}
            for(const auto& item:data){tick(span);solution->values.push_back(number(item));}
            if(entry.contains("derivatives")){const auto& derivatives=entry.at("derivatives");if(!derivatives.is_array()||(!derivatives.empty()&&derivatives.size()!=solution->values.size()))fail("R2001","Invalid saved dense derivative layout.",span);solution->derivatives.reserve(derivatives.size());
        for(const auto& derivative:derivatives){tick(span);solution->derivatives.push_back(number(derivative));}}
            if(entry.contains("sensitivity_count")){solution->sensitivityCount=savedSize(entry.at("sensitivity_count"),span);if(solution->sensitivityCount>128)fail("R2001","Saved sensitivity count exceeds128 directions.",span);
        const auto& selected=entry.at("sensitivity_parameters");const auto& sensitivity=entry.at("sensitivities");if(!selected.is_array()||selected.size()!=solution->sensitivityCount||!sensitivity.is_array()||(solution->sensitivityCount&&solution->values.size()>8000000/solution->sensitivityCount)||sensitivity.size()!=solution->values.size()*solution->sensitivityCount)fail("R2001","Invalid saved sensitivity layout.",span);
        for(const auto& value:selected){const auto index=savedInteger(value,span);if(index < -1||index>4095)fail("R2001","Invalid saved parameter sensitivity selector.",span);solution->sensitivityParameterIndices.push_back(static_cast<int>(index));}for(const auto& value:sensitivity){tick(span);solution->sensitivities.push_back(number(value));}}
            if(entry.contains("event_times")){const auto& events=entry.at("event_times");const auto& values=entry.at("event_values");const auto& indices=entry.at("event_indices");const auto& directions=entry.at("event_directions");if(!events.is_array()||events.size()>100000||!values.is_array()||values.size()!=events.size()*width||!indices.is_array()||indices.size()!=events.size()||!directions.is_array()||directions.size()!=events.size())fail("R2001","Invalid saved event layout.",span);
        const auto direction=solution->times.back()>=solution->times.front()?1:-1;double previous=solution->times.front();for(const auto& event:events){const double value=number(event);if(direction*(value-previous)<0||direction*(value-solution->times.front())<0||direction*(solution->times.back()-value)<0)fail("R2001","Saved event times must follow the retained solution interval.",span);solution->eventTimes.push_back(value);previous=value;}for(const auto& value:values){tick(span);solution->eventValues.push_back(number(value));}for(const auto& value:indices){const auto index=savedInteger(value,span);
        if(index<1||index>16)fail("R2001","Invalid saved event index.",span);
        solution->eventIndices.push_back(static_cast<int>(index));}for(const auto& value:directions){const auto directionValue=savedInteger(value,span);
        if(directionValue < -1||directionValue>1)fail("R2001","Invalid saved event direction.",span);
        solution->eventDirections.push_back(static_cast<int>(directionValue));}if(entry.contains("terminal_event")){if(!entry.at("terminal_event").is_boolean())fail("R2001","Saved terminal-event flag must be Boolean.",span);solution->stats.terminalEvent=entry.at("terminal_event").get<bool>();}}
            if(entry.contains("dense_segments")){const auto& segments=entry.at("dense_segments");if(!segments.is_array()||segments.size()>1000000)fail("R2001","Invalid saved dense segment array.",span);
        const auto direction=solution->times.back()>=solution->times.front()?1:-1;double previous=solution->times.front();for(const auto& item:segments){tick(span);ode::DenseSegment segment;segment.first=number(item.at("first"));segment.last=number(item.at("last"));segment.center=number(item.at("center"));const auto degree=savedSize(item.at("degree"),span);if(degree>12||direction*(segment.last-segment.first)<=0)fail("R2001","Invalid saved dense polynomial degree or interval.",span);
        const double tolerance=64*std::numeric_limits<double>::epsilon()*std::max({1.0,std::abs(previous),std::abs(segment.first)});if(direction*(segment.first-previous)>tolerance||direction*(segment.last-previous)<-tolerance)fail("R2001","Saved dense intervals must form an ordered contiguous sequence.",span);segment.degree=static_cast<unsigned>(degree);
        const auto& coefficients=item.at("coefficients");if(!coefficients.is_array()||coefficients.size()!=(degree+1)*width||coefficients.size()>8000000-solution->denseCoordinateCount)fail("R2001","Invalid saved dense coefficient layout/budget.",span);
        for(const auto& coefficient:coefficients)segment.coefficients.push_back(number(coefficient));
        solution->denseCoordinateCount+=segment.coefficients.size();previous=segment.last;solution->denseSegments.push_back(std::move(segment));}}
            if(solution->retainedCoordinateCount()>std::min<std::size_t>(32000000,options_.maxRetainedSolutionCoordinates))fail("R2001","Saved solution values, slopes, sensitivities, events and dense polynomials exceed the combined coordinate budget.",span);
            solution->stats.method=entry.value("method",std::string("loaded"));solution->stats.jacobian=entry.value("jacobian",std::string("loaded"));
            return SolutionValue{solution,definition};
        }
        fail("R2001","Unsupported saved data type: "+type,span);
    }
    bool validSavedField(const std::string& name)const {
        const auto tokens=language::lex(name);if(!tokens.diagnostics.empty()||tokens.tokens.size()!=2||tokens.tokens.front().span.offset!=0||tokens.tokens.front().span.length!=name.size())return false;
        const auto kind=tokens.tokens.front().kind;return kind==language::TokenKind::Identifier||kind==language::TokenKind::Keyword||kind==language::TokenKind::Basis||kind==language::TokenKind::Builtin;
    }
    bool validSavedName(const std::string& name) const {
        const auto tokens=language::lex(name);return tokens.diagnostics.empty()&&tokens.tokens.size()==2&&
            tokens.tokens.front().kind==language::TokenKind::Identifier&&tokens.tokens.front().span.length==name.size();
    }
    Value workspaceFileCall(const Expr& expression,const std::vector<Value>& supplied) {
        const auto span=expression.span;std::vector<std::string> arguments;
        for(const auto& value:supplied)arguments.push_back(std::get<std::string>(value));
        const auto path=dataPath(arguments.at(0),span);using Json=nlohmann::json;
        try{
            if(expression.text=="save"){
                Json document={{"format","PentarnionWorkspace"},{"version",1},{"algebra",result_.analysis.program.algebraId},{"gamma",context_.gamma()},{"variables",Json::object()}};
                auto saveOne=[&](const std::string& name){const auto found=variables_.find(name);if(found==variables_.end())fail("R2001","Cannot save unknown variable: "+name,span);document["variables"][name]=encodeWorkspaceValue(found->second,span);};
                if(arguments.size()>1)for(std::size_t n=1;n<arguments.size();++n)saveOne(arguments[n]);
                else for(const auto& [name,value]:variables_){if(valueType(value)==Type::OdeModel||valueType(value)==Type::Function)continue;saveOne(name);}
                const auto bytes=document.dump();dataio::writeBytesAtomic(path,bytes,dataOptions());return I{0};
            }
            std::size_t nesting=0;const auto bytes=dataio::readBytes(path,dataOptions());
            const auto document=Json::parse(bytes,[&](int depthValue,Json::parse_event_t,Json&){nesting=std::max(nesting,static_cast<std::size_t>(std::max(depthValue,0)));return nesting<32;});
            if(nesting>=32||document.at("format")!="PentarnionWorkspace"||document.at("version")!=1||document.at("algebra")!=result_.analysis.program.algebraId||document.at("gamma").get<double>()!=context_.gamma())fail("R2001","Workspace format/version/algebra does not match this session.",span);
            const auto& saved=document.at("variables");if(!saved.is_object()||saved.size()>options_.maxWorkspaceVariables)fail("R2001","Invalid workspace variable table.",span);
            Environment replacement=variables_;std::size_t matrixCoordinates=0,solutionCoordinates=0,retainedSolutionCoordinates=0;
            auto loadOne=[&](const std::string& name){if(!validSavedName(name)||!saved.contains(name))fail("R2001","Invalid or absent saved variable: "+name,span);
                Value value=decodeWorkspaceValue(saved.at(name),span);const auto existing=variables_.find(name);
                if(existing!=variables_.end()){const auto oldType=valueType(existing->second),newType=valueType(value);
                    if(oldType==newType&&(oldType==Type::Solution||oldType==Type::Record||oldType==Type::Tuple||oldType==Type::Algebra||oldType==Type::Element||oldType==Type::ElementSeries||oldType==Type::Tensor||oldType==Type::ArraySeries)&&!sameDataSchema(existing->second,value))fail("R2001","Saved data mathematical schema differs from its declared target.",span);
                    else if(oldType!=newType||oldType==Type::RealMatrix||oldType==Type::PentarnionMatrix||oldType==Type::ElementMatrix)
                        value=convert(value,oldType,span,runtimeSchema(existing->second));
                }
                replacement.insert_or_assign(name,std::move(value));};
            if(arguments.size()>1)for(std::size_t n=1;n<arguments.size();++n)loadOne(arguments[n]);
            else for(auto it=saved.begin();it!=saved.end();++it)loadOne(it.key());
            if(replacement.size()>options_.maxWorkspaceVariables)fail("R1005","Loaded workspace exceeds the variable budget.",span);
            std::unordered_set<const Matrix*> matrices;std::unordered_set<const ode::Solution*> solutions;
            const auto matrixLimit=std::min<std::size_t>(1000000,options_.maxMatrixCoordinates),solutionLimit=std::min<std::size_t>(8000000,options_.maxSolutionCoordinates);
            auto countStorage=[&](const Value& value){tick(span);
                if(const auto* array=std::get_if<MatrixValue>(&value);array&&matrices.insert(array->get()).second){const auto count=(*array)->values.size()*((*array)->pentarnion?5:1);if(count>matrixLimit-matrixCoordinates)fail("R1902","Loaded workspace exceeds the matrix coordinate budget.",span);matrixCoordinates+=count;}
                const ode::Solution* solution=nullptr;if(const auto* item=std::get_if<SolutionValue>(&value))solution=item->data.get();if(const auto* item=std::get_if<RealSeries>(&value))solution=item->data.get();if(const auto* item=std::get_if<PentSeries>(&value))solution=item->data.get();if(const auto* item=std::get_if<UniversalSeries>(&value))solution=item->data.get();
                if(solution&&solutions.insert(solution).second){if(solution->values.size()>solutionLimit-solutionCoordinates)fail("R1005","Loaded workspace exceeds the solution state-coordinate budget.",span);solutionCoordinates+=solution->values.size();
        const auto retainedLimit=std::min<std::size_t>(32000000,options_.maxRetainedSolutionCoordinates);if(solution->retainedCoordinateCount()>retainedLimit-retainedSolutionCoordinates)fail("R1005","Loaded workspace exceeds the auxiliary solution storage budget.",span);retainedSolutionCoordinates+=solution->retainedCoordinateCount();}};
            for(const auto& [name,value]:replacement){(void)name;countStorage(value);}
            for(const auto* frame:outerFrames_)for(const auto& [name,value]:*frame){(void)name;countStorage(value);}
            Environment original=std::move(variables_);variables_=std::move(replacement);
            try{checkFoundationWorkspace("",I(0),span);}catch(...){variables_=std::move(original);throw;}return I{0};
        }catch(const dataio::Error& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R2001",error.what(),span);}
        catch(const nlohmann::json::exception& error){fail("R2001",std::string("Invalid saved workspace: ")+error.what(),span);}
    }

