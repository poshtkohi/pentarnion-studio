    bool compatibleSchema(const std::shared_ptr<const ValueSchema>& expected,const std::shared_ptr<const ValueSchema>& actual,std::size_t depth=0)const {
        if(!expected)return true;
        if(!actual)return !expected->algebra&&!expected->function&&expected->fields.empty()&&expected->elements.empty();
        if(depth>32)return false;
        if(expected->tensorEntryType!=actual->tensorEntryType)return false;
        if(!expected->shape.empty()&&!actual->shape.empty()&&expected->shape!=actual->shape)return false;
        if(expected->algebra&&(!actual->algebra||expected->algebra->hash()!=actual->algebra->hash()))return false;
        if(expected->seriesElementType!=Type::Invalid&&expected->seriesElementType!=actual->seriesElementType)return false;
        if(expected->fields.size()!=actual->fields.size()||expected->elements.size()!=actual->elements.size())return false;
        for(const auto& a:expected->fields){const auto b=std::find_if(actual->fields.begin(),actual->fields.end(),[&](const auto& field){return field.name==a.name;});if(b==actual->fields.end()||!typeAssignable(b->type,a.type)||!compatibleSchema(a.schema,b->schema,depth+1))return false;}
        for(std::size_t n=0;n<expected->elements.size();++n)if(!typeAssignable(actual->elements[n].type,expected->elements[n].type)||!compatibleSchema(expected->elements[n].schema,actual->elements[n].schema,depth+1))return false;
        if(expected->function){
            if(!actual->function)return false;
            const auto& a=*expected->function;const auto& b=*actual->function;
            if(a.result!=b.result||a.parameters.size()!=b.parameters.size()||a.results!=b.results||!compatibleSchema(a.resultSchema,b.resultSchema,depth+1))return false;
            for(std::size_t n=0;n<a.parameters.size();++n){
                const auto as=n<a.parameterSchemas.size()?a.parameterSchemas[n]:a.parameters[n].schema;
                const auto bs=n<b.parameterSchemas.size()?b.parameterSchemas[n]:b.parameters[n].schema;
                if(a.parameters[n].type!=b.parameters[n].type||!compatibleSchema(as,bs,depth+1))return false;
            }
        }
        return true;
    }
    bool constantDefault(const ExprPtr& value)const {
        switch(value->kind){case ExprKind::IntegerLiteral:case ExprKind::RealLiteral:case ExprKind::StringLiteral:return true;
        case ExprKind::Group:case ExprKind::Unary:case ExprKind::Binary:case ExprKind::TupleLiteral:case ExprKind::PentConstructor:case ExprKind::MatrixLiteral:break;
        case ExprKind::Call:if(value->text!="options"&&value->text!="complex"&&value->text!="iterationinfo")return false;break;
        default:return false;}
        return std::all_of(value->children.begin(),value->children.end(),[&](const auto& item){return constantDefault(item);});
    }
    void checkNativeDefinition(const FunctionDefinition& function) {
        auto error=[&](const std::string& message,SourceSpan span){add("E1960",message,span);};
        auto supported=[](Type type){return type==Type::Real||type==Type::Pentarnion||type==Type::Complex||type==Type::Element||type==Type::RealMatrix||type==Type::PentarnionMatrix||type==Type::ElementMatrix||type==Type::Tensor;};
        const bool outputsOkay=function.results.empty()?supported(function.result):std::all_of(function.results.begin(),function.results.end(),supported);
        if(!outputsOkay||!function.captureNames.empty())error("Native functions require checked numeric scalar/fixed-array outputs and cannot capture values.",function.span);
        if(function.results.empty()){if(!coordinateWidth(function.result,function.resultSchema))error("A native array result requires an explicit fixed shape.",function.span);}
        else for(std::size_t n=0;n<function.results.size();++n){const auto schema=function.resultSchema&&n<function.resultSchema->elements.size()?function.resultSchema->elements[n].schema:nullptr;if(!coordinateWidth(function.results[n],schema))error("Every native array tuple result requires an explicit fixed shape.",function.span);}
        std::size_t coordinates=0;for(const auto& parameter:function.parameters){const auto width=coordinateWidth(parameter.type,parameter.schema);if(!supported(parameter.type)||!width)error("Native parameters require a complete real, complex, Pentarnion or qualified element type.",parameter.span);coordinates+=width;}
        if(function.parameters.size()>64||coordinates>128)error("Native parameters exceed the 64-parameter/128-coordinate budget.",function.span);
        std::function<std::optional<long long>(const ExprPtr&)> literal=[&](const ExprPtr& value)->std::optional<long long>{if(value->kind==ExprKind::Group)return literal(value->children[0]);if(value->kind==ExprKind::Unary&&(value->text=="+"||value->text=="-")){auto result=literal(value->children[0]);if(result&&value->text=="-")*result=-*result;return result;}if(value->kind!=ExprKind::IntegerLiteral)return {};long long result=0;const auto parsed=std::from_chars(value->text.data(),value->text.data()+value->text.size(),result);if(parsed.ec!=std::errc())return {};return result;};
        std::size_t nativeNodes=0;bool nativeLimit=false;
        const std::unordered_set<std::string> calls{"pent","complex","element","algebra_basis","algebra_coefficients","coeff","scalar","pure","conj","norm2","inv","commutator","associator","sin","cos","exp","sinh","cosh","tan","asin","acos","atan","tanh","asinh","acosh","atanh","log","log10","log2","sqrt","abs","floor","ceil","round","expm1","log1p","erf","erfc","hypot","atan2","realpart","imagpart","transpose","size","numel","length","sum","mean","norm","tensor","ndims","tensorcoords","tensorvalues"};
        std::function<void(const ExprPtr&)> expression=[&](const ExprPtr& value){if(!value||nativeLimit)return;if(++nativeNodes>20000){nativeLimit=true;error("Native function expression/statement node limit exceeded.",value->span);return;}if(!supported(value->type)&&value->type!=Type::Integer&&value->type!=Type::Algebra&&value->kind!=ExprKind::MatrixLiteral&&value->kind!=ExprKind::StringLiteral)error("Native expressions require supported numeric scalar or fixed-array types.",value->span);
            switch(value->kind){case ExprKind::IntegerLiteral:case ExprKind::RealLiteral:case ExprKind::Reference:case ExprKind::Group:case ExprKind::EndIndex:break;
            case ExprKind::StringLiteral:if(value->text!="fro"&&value->text!="real"&&value->text!="complex"&&value->text!="pentarnion"&&value->text!="all")error("Native numeric metadata only accepts the literal Frobenius norm selector.",value->span);break;
            case ExprKind::Index:if(value->children.empty()||!(universalArrayType(value->children.front()->type)||value->children.front()->type==Type::Tensor))error("Native indexing requires a checked fixed array.",value->span);for(std::size_t n=1;n<value->children.size();++n)if(value->children[n]->kind!=ExprKind::EndIndex){const auto index=literal(value->children[n]);if(!index||*index<1)error("Native array indices must be literal positive integers or end.",value->span);}break;
            case ExprKind::Transpose:if(value->children.size()!=1||!universalArrayType(value->children.front()->type))error("Native transpose requires a fixed array.",value->span);break;
            case ExprKind::Unary:if(value->text!="+"&&value->text!="-"&&value->text!="!"&&value->text!="~")error("Unsupported native unary operator.",value->span);break;
            case ExprKind::Binary:{const bool arrays=std::any_of(value->children.begin(),value->children.end(),[](const auto& child){return universalArrayType(child->type)||child->type==Type::Tensor;});const std::unordered_set<std::string> ops{"+","-","*","/","^","==","!=","~=","<","<=",">",">=","&&","||"};if(!ops.contains(value->text)&&!(arrays&&(value->text==".*"||value->text=="./"||value->text==".^")))error("Unsupported native operator.",value->span);if(value->type==Type::Integer&&(value->text=="+"||value->text=="-"||value->text=="*"||value->text=="^"))error("Use real literals or parameters for computed native arithmetic.",value->span);if((value->text=="/"||value->text=="./")&&!scalarType(value->children[1]->type)&&!(arrays&&value->text=="./"&&value->children[1]->type==Type::RealMatrix))error("Native division requires a real scalar or entrywise real array denominator.",value->span);if(value->text=="^"||value->text==".^"){const auto n=literal(value->children[1]);if(!n||*n < -100000||*n>100000)error("Native powers require an integer literal in [-100000,100000].",value->span);}break;}
            case ExprKind::PentConstructor:break;
            case ExprKind::Call:if(!calls.contains(value->text))error("Unsupported native call: "+value->text,value->span);if(value->text=="coeff"&&value->children.size()==2){const auto n=literal(value->children[1]);const auto type=value->children[0]->type;const auto width=type==Type::Tensor?coordinateWidth(value->children[0]->schema->tensorEntryType,value->children[0]->schema):type==Type::Element||type==Type::ElementMatrix?coordinateWidth(Type::Element,value->children[0]->schema):type==Type::RealMatrix||scalarType(type)?1:5;if(!n||*n<0||std::size_t(*n)>=width)error("Native coeff index must be a literal inside the entry algebra dimension.",value->span);}break;
            case ExprKind::Member:if(value->children.empty()||value->children[0]->type!=Type::Algebra||!value->schema||!value->schema->algebra)error("Native members require an immutable algebra basis.",value->span);break;
            case ExprKind::MatrixLiteral:if(value->children.size()>128||!std::all_of(value->children.begin(),value->children.end(),[](const auto& child){return scalarType(child->type)||child->type==Type::Pentarnion||child->type==Type::Element;}))error("Native array literals require at most128 scalar algebra cells; dynamic block/range expansion is unsupported.",value->span);break;
            default:error("Native functions do not support arrays, callbacks, records or imports.",value->span);}
            for(const auto& child:value->children)expression(child);};
        std::function<void(const std::vector<Statement>&)> statements=[&](const auto& body){for(const auto& statement:body){if(nativeLimit)return;if(++nativeNodes>20000){nativeLimit=true;error("Native function expression/statement node limit exceeded.",statement.span);return;}switch(statement.kind){case StatementKind::Declaration:if(!supported(statement.declaredType))error("Native locals require a supported numeric scalar or fixed array.",statement.span);break;case StatementKind::IndexedAssignment:if((!universalArrayType(statement.declaredType)&&statement.declaredType!=Type::Tensor)||statement.arguments.empty()||statement.arguments.size()>(statement.declaredType==Type::Tensor?8u:2u))error("Native indexed assignment requires one linear or two row,column selectors.",statement.span);for(const auto& selector:statement.arguments)if(selector->kind!=ExprKind::EndIndex){const auto index=literal(selector);if(!index||*index<1)error("Native indexed assignment requires literal positive indices or end.",selector->span);}break;case StatementKind::Assignment:case StatementKind::If:case StatementKind::Return:break;default:error("Native bodies support declarations, assignment, indexed assignment, if/else and return.",statement.span);}if(statement.kind==StatementKind::Return&&!function.results.empty()&&statement.expression&&statement.expression->kind==ExprKind::TupleLiteral){for(const auto& item:statement.expression->children)expression(item);}else expression(statement.expression);statements(statement.body);statements(statement.alternative);}};statements(function.body);
    }
    static bool typeAssignable(Type actual,Type target) {
        return actual==target||(target==Type::Real&&actual==Type::Integer)||
            (target==Type::Complex&&scalarType(actual))||(target==Type::Element&&scalarType(actual))||
            (target==Type::Pentarnion&&scalarType(actual))||
            (target==Type::RealMatrix&&(rangeType(actual)||actual==Type::LogicalMatrix))||
            (target==Type::PentarnionMatrix&&(actual==Type::RealMatrix||actual==Type::LogicalMatrix));
    }
    std::shared_ptr<const ValueSchema> callableSchema(const std::shared_ptr<const FunctionDefinition>& definition) {
        if(!definition)return {};
        auto prototype=std::make_shared<FunctionDefinition>(*definition);prototype->body.clear();prototype->captureNames.clear();
        auto schema=std::make_shared<ValueSchema>();schema->function=std::move(prototype);return schema;
    }
    bool hasNamedArguments(const ExprPtr& value)const {
        return std::any_of(value->argumentNames.begin(),value->argumentNames.end(),[](const auto& name){return !name.empty();});
    }
    void reorderCallArguments(const ExprPtr& value,const FunctionDefinition& function,std::size_t skip=0) {
        
        std::vector<ExprPtr> ordered(function.parameters.size());std::size_t position=0;bool named=false;
        for(std::size_t n=skip;n<value->children.size();++n){
            const auto slot=n-skip;const std::string key=slot<value->argumentNames.size()?value->argumentNames[slot]:std::string();
            std::size_t target=position++;
            if(key.empty()){if(named)add("E1205","Positional arguments must precede named arguments.",value->span);}
            else{named=true;target=function.parameters.size();for(std::size_t p=0;p<function.parameters.size();++p)if(function.parameters[p].name==key){target=p;break;}}
            if(target>=ordered.size()){add("E1205","Unknown or excess function argument: "+key,value->span);continue;}
            if(ordered[target])add("E1205","Function argument was supplied more than once.",value->span);
            else ordered[target]=value->children[n];
        }
        for(std::size_t n=0;n<ordered.size();++n)if(!ordered[n]&&n<function.parameterDefaults.size())ordered[n]=function.parameterDefaults[n];
        if(std::any_of(ordered.begin(),ordered.end(),[](const auto& child){return !child;})){add("E1205","All function parameters must be supplied.",value->span);return;}
        if(skip)ordered.insert(ordered.begin(),value->children.front());
        value->children=std::move(ordered);value->argumentNames.clear();
    }
    ExprPtr functionHandle() {
        const auto start=expect("@");
        if(!match("(")){const auto identifier=name();return node(ExprKind::FunctionHandle,cover(start.span,identifier.span),std::string(text(identifier)));}
        auto function=std::make_shared<FunctionDefinition>();function->name="<anonymous>";function->origin=sourceOrigin();
        const auto outer=m_bindings;std::unordered_set<std::string> parameters;
        if(!is(")"))do{const auto type=declaredTypeName();const auto descriptor=m_lastDeclaredSchema;const auto identifier=name();const std::string spelling(text(identifier));
            if(type==Type::Void||protectedName(identifier)||!parameters.insert(spelling).second)add("E1204","Anonymous parameters require distinct typed names.",identifier.span);
            function->parameters.push_back({spelling,type,identifier.span,descriptor});function->parameterSchemas.push_back(descriptor);m_bindings.insert_or_assign(spelling,Binding{type,identifier.span,{},{},descriptor});
        }while(match(","));expect(")");expect("=>");
        auto body=expression();function->result=check(body,true);function->resultSchema=body->schema;
        Statement returned;returned.kind=StatementKind::Return;returned.expression=body;returned.span=body->span;function->body.push_back(returned);function->span=cover(start.span,body->span);
        std::unordered_set<std::string> captures;
        std::function<void(const ExprPtr&)> visit=[&](const ExprPtr& item){
            if((item->kind==ExprKind::Reference||item->kind==ExprKind::Call||item->kind==ExprKind::FunctionHandle)&&!parameters.contains(item->text)&&outer.contains(item->text)&&outer.at(item->text).type!=Type::Algebra)captures.insert(item->text);
            for(const auto& child:item->children)visit(child);
        };visit(body);
        for(const auto& name:captures)function->captureNames.push_back(name);
        std::sort(function->captureNames.begin(),function->captureNames.end());m_bindings=outer;
        auto value=node(ExprKind::Lambda,function->span);value->function=function;value->schema=callableSchema(function);return value;
    }
    Type checkFunctionHandle(const ExprPtr& value) {
        if(const auto found=m_bindings.find(value->text);found!=m_bindings.end()&&found->second.type==Type::Function){
            value->schema=found->second.schema?found->second.schema:callableSchema(found->second.function);value->function=value->schema?value->schema->function:nullptr;return Type::Function;}
        for(const auto& signature:signatures)if(signature.name==value->text){
            auto function=std::make_shared<FunctionDefinition>();function->name=value->text;function->result=signature.result;
            for(std::size_t n=0;n<signature.arity;++n)function->parameters.push_back({"arg"+std::to_string(n+1),signature.parameters[n]==Parameter::Pentarnion?Type::Pentarnion:signature.parameters[n]==Parameter::Integer?Type::Integer:Type::Real,{}});
            value->function=function;value->schema=callableSchema(function);return Type::Function;
        }
        add("E1204","A function handle requires a declared function or supported mathematical builtin.",value->span);return Type::Invalid;
    }
    Type checkInvoke(const ExprPtr& value,Type expected) {
        const auto type=check(value->children.front(),true);
        if(type!=Type::Function){add("E1204","Only function handles can be invoked.",value->span);return Type::Invalid;}
        const auto schema=value->children.front()->schema;
        const auto function=schema?schema->function:value->children.front()->function;
        for(std::size_t n=1;n<value->children.size();++n)check(value->children[n],true);
        if(!function)return expected==Type::Invalid?Type::Real:expected;
        reorderCallArguments(value,*function,1);
        if(value->children.size()!=function->parameters.size()+1){add("E1205","Incorrect callback argument count.",value->span);return Type::Invalid;}
        for(std::size_t n=0;n<function->parameters.size();++n)if(!assignable(value->children[n+1],value->children[n+1]->type,function->parameters[n].type))add("E1204","Callback argument type does not match.",value->children[n+1]->span);
        for(std::size_t n=0;n<function->parameters.size();++n){const auto expectedSchema=n<function->parameterSchemas.size()?function->parameterSchemas[n]:function->parameters[n].schema;const auto& argument=value->children[n+1];if(!(function->parameters[n].type==Type::Element&&scalarType(argument->type))&&!compatibleSchema(expectedSchema,argument->schema))add("E2530","Callback argument belongs to a different algebra or structured schema.",argument->span);}
        value->schema=function->resultSchema;return function->result;
    }
    Statement foundationDeclaration() {
        const auto beginning=current();const Type declared=declaredTypeName();const auto identifier=name();
        const std::string spelling(text(identifier));const bool invalid=protectedName(identifier)||m_bindings.contains(spelling);
        if(invalid)add("E1203","Declaration requires a new unprotected name.",identifier.span);
        expect("=");auto expressionValue=expression();const Type actual=check(expressionValue,true,false,false,declared);
        if(!assignable(expressionValue,actual,declared))add("E1204","Initializer has an incompatible declared type.",expressionValue->span);
        if(!invalid){m_bindings.emplace(spelling,Binding{declared,identifier.span,expressionValue->ode,expressionValue->function,expressionValue->schema});m_result.symbols.push_back({spelling,declared,identifier.span,expressionValue->ode,expressionValue->function,expressionValue->schema});}
        Statement result;result.kind=StatementKind::Declaration;result.name=spelling;result.nameSpan=identifier.span;result.declaredType=declared;result.expression=expressionValue;result.span=terminate(beginning.span,expressionValue->span);return result;
    }
    bool destructureAhead()const {
        std::size_t cursor=m_index+1,depth=1;
        while(cursor<m_tokens.size()&&depth){const auto token=text(m_tokens[cursor]);if(token=="[")++depth;else if(token=="]")--depth;++cursor;}
        return depth==0&&cursor<m_tokens.size()&&text(m_tokens[cursor])=="=";
    }
    Statement destructureStatement() {
        const auto beginning=expect("[");Statement result;result.kind=StatementKind::Destructure;
        auto targets=std::make_shared<ValueSchema>();
        std::unordered_set<std::string> distinct;
        if(!is("]"))do{Type type=Type::Invalid;const auto first=current();std::shared_ptr<const ValueSchema> descriptor;
            if(is("integer")||is("real")||is("pentarnion")||is("complex")||is("string")||is("callback")||is("options")||is("record")||is("matrix")||is("vector")||is("realmatrix")||is("pentarnionmatrix")||is("logicalmatrix")||is("element")||is("algebra")){type=declaredTypeName();descriptor=m_lastDeclaredSchema;}
            const auto identifier=name();const std::string spelling(text(identifier));
            if(protectedName(identifier)||!distinct.insert(spelling).second)add("E1203","Output targets need distinct unprotected names.",identifier.span);
            if(type==Type::Invalid){const auto found=m_bindings.find(spelling);if(found==m_bindings.end())add("E1201","Untyped output target needs an earlier declaration.",identifier.span);else{type=found->second.type;descriptor=found->second.schema;}}
            else if(m_bindings.contains(spelling))add("E1202","A typed output target must be a new declaration.",first.span);
            result.names.push_back(spelling);result.declaredTypes.push_back(type);
            targets->elements.push_back({spelling,type,descriptor});
        }while(match(","));expect("]");expect("=");result.expression=expression();const auto type=check(result.expression,true);
        const auto schema=result.expression->schema;
        if(type!=Type::Tuple||!schema||schema->elements.size()!=result.names.size())add("E1204","Destructuring requires the matching number of returned outputs.",result.expression->span);
        else for(std::size_t n=0;n<result.names.size();++n){if(!typeAssignable(schema->elements[n].type,result.declaredTypes[n]))add("E1204","Output type does not match the target.",result.expression->span);
            const auto actualSchema=schema->elements[n].schema;auto& expected=targets->elements[n].schema;
            if(!expected)expected=actualSchema;
            else if(!(result.declaredTypes[n]==Type::Element&&scalarType(schema->elements[n].type))&&!compatibleSchema(expected,actualSchema))add("E2530","Output belongs to a different algebra or structured schema.",result.expression->span);
            if(result.declaredTypes[n]==Type::Element&&(!expected||!expected->algebra))add("E2530","An element output requires a qualified or inferred algebra.",result.expression->span);
            if(!m_bindings.contains(result.names[n])){m_bindings.emplace(result.names[n],Binding{result.declaredTypes[n],beginning.span,{},expected?expected->function:nullptr,expected});m_result.symbols.push_back({result.names[n],result.declaredTypes[n],beginning.span,{},expected?expected->function:nullptr,expected});}}
        result.declaredSchema=targets;
        result.span=terminate(beginning.span,result.expression->span);return result;
    }
    Statement importStatement() {
        const auto beginning=expect("import");const auto path=advance();if(path.kind!=TokenKind::String)syntax("E1204","import requires a quoted .p path.",path.span);
        const std::string requested=decoded(text(path));expect("as");const auto alias=name();const std::string aliasName(text(alias));
        if(protectedName(alias)||m_bindings.contains(aliasName))add("E1203","Import namespace needs a new unprotected name.",alias.span);
        Statement result;result.kind=StatementKind::Import;result.name=aliasName;result.nameSpan=alias.span;result.span=terminate(beginning.span,alias.span);
        if(m_blockDepth||m_functionDepth){add("E1601","Imports are only allowed at file/command scope.",beginning.span);return result;}
        if(m_options.importStack.size()>=std::min<std::size_t>(16,m_options.maximumImportDepth)){add("E1103","Module import depth exceeded.",beginning.span);return result;}
        try{
            ModuleSource source;
            if(requested.empty()||requested.find('\0')!=std::string::npos||std::filesystem::path(requested).extension()!=".p")throw std::runtime_error("Imports require a nonempty .p source path.");
            if(m_options.moduleResolver){const auto loaded=m_options.moduleResolver(requested,m_options.sourceDirectory);if(!loaded)throw std::runtime_error("Module resolver could not load the source.");source=*loaded;}
            else{
                auto file=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(requested.data()),requested.size()));
                if(file.extension()!=".p")throw std::runtime_error("Imported modules must be .p source files.");
                if(file.is_relative())file=m_options.sourceDirectory/file;
                file=std::filesystem::canonical(file);const auto name=file.u8string();source.identity.assign(name.begin(),name.end());
                const auto length=std::filesystem::file_size(file);if(length>m_options.maximumSourceBytes)throw std::runtime_error("Imported source exceeds its byte budget.");
                std::ifstream stream(file,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read imported source.");source.source.assign(std::istreambuf_iterator<char>(stream),{});
            }
            if(source.identity.empty()||source.source.size()>m_options.maximumSourceBytes)throw std::runtime_error("Module identity/source exceeds the source contract.");
            if(!m_importedIdentities.insert(source.identity).second)throw std::runtime_error("Duplicate import in this source; reuse its namespace.");
            if(std::find(m_options.importStack.begin(),m_options.importStack.end(),source.identity)!=m_options.importStack.end())throw std::runtime_error("Cyclic source import detected.");
            if(source.source.size()>m_options.maximumImportBytes-std::min(m_importBudget->bytes,m_options.maximumImportBytes))throw std::runtime_error("Aggregate imported source exceeds 8 MiB budget.");
            m_importBudget->bytes+=source.source.size();
            auto options=m_options;options.importBudget=m_importBudget;options.initialSymbols.clear();options.importStack.push_back(source.identity);
            options.sourceIdentity=source.identity;
            options.sourceDirectory=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(source.identity.data()),source.identity.size())).parent_path();
            auto dependency=std::make_shared<const std::string>(std::move(source.source));
            m_result.dependencies.push_back({source.identity,dependency});
            const auto checked=analyse(*dependency,options);
            m_result.dependencies.insert(m_result.dependencies.end(),checked.dependencies.begin(),checked.dependencies.end());
            if(!checked.ok()){for(const auto& diagnostic:checked.diagnostics){const auto location=sourceLocation(*dependency,diagnostic.span.offset);add(diagnostic.code,"In module "+source.identity+":"+std::to_string(location.line)+":"+std::to_string(location.column)+": "+diagnostic.message+" (imported at this statement)",path.span,diagnostic.severity);}return result;}
            if(checked.program.algebraId!=m_result.program.algebraId||checked.program.gamma!=m_result.program.gamma)throw std::runtime_error("Module algebra context differs from its importing file.");
            auto schema=std::make_shared<ValueSchema>();for(const auto& symbol:checked.symbols){auto shape=symbol.schema;if(symbol.function&&!shape)shape=callableSchema(symbol.function);schema->fields.push_back({symbol.name,symbol.type,shape});}
            m_bindings.emplace(aliasName,Binding{Type::Record,alias.span,{},{},schema});m_result.symbols.push_back({aliasName,Type::Record,alias.span,{},{},schema});
            result.module=std::make_shared<Program>(checked.program);result.moduleId=source.identity;
        }catch(const std::exception& error){add("E1950",std::string("Cannot import module: ")+error.what(),path.span);}
        return result;
    }
    Type checkFoundationCall(const ExprPtr& value) {
        const auto& name=value->text;const auto count=value->children.size();bool valid=true;
        auto require=[&](bool condition,const char* message){if(!condition){add("E1205",message,value->span);valid=false;}};
        auto realArgument=[&](std::size_t n){if(n<count&&!scalarType(value->children[n]->type)&&value->children[n]->type!=Type::Invalid){add("E1204","This argument must be a real scalar.",value->children[n]->span);valid=false;}};
        auto matrixArgument=[&](std::size_t n,bool allowPent=true){if(n>=count)return;const auto t=value->children[n]->type;if(!matrixType(t)&&!rangeType(t)&&t!=Type::RealSeries&&t!=Type::PentarnionSeries&&t!=Type::Invalid){add("E1204","This argument requires an array or sampled series.",value->children[n]->span);valid=false;}if(!allowPent&&(t==Type::PentarnionMatrix||t==Type::PentarnionSeries)){add("E1204","This operation requires real values.",value->span);valid=false;}};
        auto arrayType=[&](std::size_t n){if(n>=count)return Type::Invalid;const auto t=value->children[n]->type;return t==Type::PentarnionMatrix||t==Type::PentarnionSeries?Type::PentarnionMatrix:Type::RealMatrix;};
        auto tuple=[&](Type type){auto schema=std::make_shared<ValueSchema>();schema->elements={{"first",type,{}},{"second",type,{}}};value->schema=schema;return Type::Tuple;};
        if(name=="options"||name=="with_options"){
            const auto start=name=="options"?0u:1u;if(start){require(count>0,"with_options requires defaults.");if(count&&value->children[0]->type!=Type::Record){add("E1204","Defaults must be an options record.",value->span);valid=false;}}
            auto schema=std::make_shared<ValueSchema>();if(start&&count&&value->children[0]->schema)*schema=*value->children[0]->schema;
            std::unordered_set<std::string> supplied;
            for(std::size_t n=start;n<count;++n){const std::string key=n<value->argumentNames.size()?value->argumentNames[n]:std::string();
                if(key.empty()||!supplied.insert(key).second){add("E1205","Options require unique named fields.",value->span);valid=false;continue;}
                auto found=std::find_if(schema->fields.begin(),schema->fields.end(),[&](const auto& field){return field.name==key;});
                if(start){if(found==schema->fields.end()){add("E1205","Unknown option: "+key,value->span);valid=false;}else if(!typeAssignable(value->children[n]->type,found->type)){add("E1204","Option type differs from its default.",value->span);valid=false;}}
                else schema->fields.push_back({key,value->children[n]->type,value->children[n]->schema});
            }
            if(schema->fields.size()>64){add("E1103","An options record is limited to 64 fields.",value->span);valid=false;}value->schema=schema;return valid?Type::Record:Type::Invalid;
        }
        if(hasNamedArguments(value)){add("E1205","This builtin accepts positional arguments only; pass an options record.",value->span);valid=false;}
        Type result=Type::Real;
        if(name=="complex"){require(count==1||count==2,"complex requires real and optional imaginary parts.");for(std::size_t n=0;n<count;++n)realArgument(n);result=Type::Complex;}
        else if(name=="realpart"||name=="imagpart"||name=="arg"){require(count==1,"This complex query requires one argument.");if(count&&value->children[0]->type!=Type::Complex&&!scalarType(value->children[0]->type)){add("E1204","A complex or real scalar is required.",value->span);valid=false;}}
        else if(name=="threads"){require(count<=1,"threads() queries, threads(n) sets 0..64 threads.");if(count){if(value->children[0]->type!=Type::Integer){add("E1204","Thread count requires Integer type.",value->span);valid=false;}result=Type::Void;}else result=Type::Integer;}
        else if(name=="parallel_info"){require(count==0,"parallel_info takes no arguments.");result=Type::Record;auto schema=std::make_shared<ValueSchema>();for(const auto* key:{"requested","effective","last_requested","last_effective","threads_used","peak_active","parallel_calls","serial_calls","last_elements","module_loaded","linalg_module_loaded","roots_module_loaded","fitting_module_loaded","optimization_module_loaded","maps_module_loaded","threshold"})schema->fields.push_back({key,Type::Integer,{}});value->schema=schema;}
        else if(name=="reshape"){require(count==2||count==3,"Use reshape(A,rows,columns) or reshape(A,[rows,columns]).");matrixArgument(0);result=count&&value->children[0]->type==Type::LogicalMatrix?Type::LogicalMatrix:arrayType(0);}
        else if(name=="diag"){require(count==1||count==2,"diag(A[,offset]) requires one or two arguments.");matrixArgument(0);result=count&&value->children[0]->type==Type::LogicalMatrix?Type::LogicalMatrix:arrayType(0);}
        else if(name=="meshgrid"||name=="ndgrid"){require(count==1||count==2,"Grid constructors currently accept one or two real axes.");for(std::size_t n=0;n<count;++n)matrixArgument(n,false);result=name=="ndgrid"&&count==1?Type::RealMatrix:tuple(Type::RealMatrix);}
        else if(name=="find"||name=="logical"){require(count==1,"Use find(A) or logical(A).");if(!(name=="logical"&&count&&scalarType(value->children[0]->type)))matrixArgument(0);result=name=="logical"?Type::LogicalMatrix:Type::RealMatrix;}
        else if(name=="dot"||name=="norm"){require(name=="dot"?count==2:count==1||count==2,"Use dot(a,b) or norm(a[,p]).");matrixArgument(0,false);if(name=="dot")matrixArgument(1,false);}
        else if(name=="interp1"){require(count>=3&&count<=5,"Use interp1(x,y,xq[,method[,extrapolation]]).");matrixArgument(0,false);matrixArgument(1);result=count>2&&scalarType(value->children[2]->type)?(arrayType(1)==Type::PentarnionMatrix?Type::Pentarnion:Type::Real):arrayType(1);}
        else if(name=="deval"){require(count==2||count==3,"Use deval(solution,tq[,stateName]).");if(count&&value->children[0]->type!=Type::Solution){add("E1204","deval requires an ODE solution.",value->span);valid=false;}Type state=Type::Real;
            if(count&&value->children[0]->ode){const auto& states=value->children[0]->ode->states;if(count==2){if(states.size()!=1){add("E1205","Select a state name for a multi-state solution.",value->span);valid=false;}else{state=states[0].type;value->schema=states[0].schema;}}else if(value->children[2]->kind==ExprKind::StringLiteral){bool found=false;for(const auto& field:states)if(field.name==value->children[2]->text){state=field.type;value->schema=field.schema;found=true;}if(!found){add("E1204","Unknown solution state.",value->span);valid=false;}}else{add("E1204","deval state name must be a literal string.",value->span);valid=false;}}
            result=count>1&&scalarType(value->children[1]->type)?state:(state==Type::Pentarnion?Type::PentarnionMatrix:Type::RealMatrix);if(result!=state)value->schema={};}
        else if(name=="integral"){require(count==3||count==4,"Use integral(callback,a,b[,options]).");if(count&&value->children[0]->type!=Type::Function){add("E1204","integral requires a function handle.",value->span);valid=false;}realArgument(1);realArgument(2);if(count==4&&value->children[3]->type!=Type::Record){add("E1204","Quadrature options require a record.",value->span);valid=false;}const auto schema=count?value->children[0]->schema:nullptr;if(schema&&schema->function){const auto& fn=*schema->function;result=fn.result;if(fn.parameters.size()!=1||(fn.parameters[0].type!=Type::Real&&fn.parameters[0].type!=Type::Pentarnion)){add("E1204","Quadrature callbacks require exactly one real-compatible parameter.",value->span);valid=false;}}if(result!=Type::Real&&result!=Type::Integer&&result!=Type::Pentarnion){add("E1204","Quadrature callbacks must return real or Pentarnion scalars.",value->span);valid=false;}if(result==Type::Integer)result=Type::Real;}
        else if(name=="trapz"||name=="cumtrapz"){require(count>=1&&count<=3,"Use trapz/cumtrapz(y), (x,y), or (x,y,dim).");matrixArgument(count==1?0:1);const auto type=arrayType(count==1?0:1);result=name=="cumtrapz"||count==3?type:(type==Type::PentarnionMatrix?Type::Pentarnion:Type::Real);}
        else if(name=="gradient"){require(count>=1&&count<=3,"Use gradient(F[,h]) or gradient(F,hx,hy).");matrixArgument(0);result=tuple(arrayType(0));}
        else if(name=="diff"||name=="divergence"||name=="laplacian"){require(name=="divergence"?(count>=2&&count<=4):(count>=1&&count<=3),"Incorrect numerical operator argument count.");matrixArgument(0);if(name=="divergence")matrixArgument(1);result=arrayType(0);}
        else if(name=="expm1"||name=="log1p"||name=="erf"||name=="erfc"||name=="sign"){require(count==1,"This function requires one real value or real array.");if(count){const auto type=value->children[0]->type;if(type==Type::RealMatrix||type==Type::LogicalMatrix||rangeType(type))result=Type::RealMatrix;else realArgument(0);}}
        else if(name=="axpy"){require(count==3,"Use axpy(alpha,x,y).");if(count&& (!numericType(value->children[0]->type)||value->children[0]->type==Type::Complex)){add("E1204","axpy alpha must be a numeric scalar.",value->span);valid=false;}matrixArgument(1);matrixArgument(2);result=(count&&pentarnionType(value->children[0]->type))||arrayType(1)==Type::PentarnionMatrix||arrayType(2)==Type::PentarnionMatrix?Type::PentarnionMatrix:Type::RealMatrix;}
        else if(name=="hypot"){require(count==2,"hypot requires two real scalars.");realArgument(0);realArgument(1);}
        else if(name=="get_option"){require(count==2,"get_option(record,literalName) requires two arguments.");if(count==2&&value->children[0]->type==Type::Record&&value->children[0]->schema&&value->children[1]->kind==ExprKind::StringLiteral){bool found=false;for(const auto& field:value->children[0]->schema->fields)if(field.name==value->children[1]->text){result=field.type;value->schema=field.schema;found=true;}if(!found){add("E1204","Unknown option field.",value->span);valid=false;}}else{add("E1204","get_option requires a typed record and literal field name.",value->span);valid=false;}}
        return valid?result:Type::Invalid;
    }
