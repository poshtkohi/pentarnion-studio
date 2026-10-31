    std::shared_ptr<const ValueSchema> universalSchema(algebra::universal::DefinitionPtr definition) const {
        auto schema=std::make_shared<ValueSchema>();schema->algebra=std::move(definition);return schema;
    }
    std::optional<long long> universalIntegerLiteral(const ExprPtr& expression) const {
        if(!expression)return {};
        if(expression->kind==ExprKind::Group)return universalIntegerLiteral(expression->children.front());
        if(expression->kind==ExprKind::Unary&&(expression->text=="+"||expression->text=="-")){
            auto value=universalIntegerLiteral(expression->children.front());
            if(value&&expression->text=="-"){if(*value==std::numeric_limits<long long>::min())return {};*value=-*value;}return value;
        }
        if(expression->kind!=ExprKind::IntegerLiteral)return {};
        long long value=0;const auto parsed=std::from_chars(expression->text.data(),expression->text.data()+expression->text.size(),value);
        if(parsed.ec!=std::errc{}||parsed.ptr!=expression->text.data()+expression->text.size())return {};
        return value;
    }
    algebra::universal::DefinitionPtr universalDefinition(const ExprPtr& expression) const {
        return expression&&expression->schema?expression->schema->algebra:algebra::universal::DefinitionPtr{};
    }
    static bool universalFirstUnit(const algebra::universal::Definition& definition) {
        const auto& unit=definition.unit();return unit.size()==definition.dimension()&&!unit.empty()&&unit.front()==1&&
            std::all_of(unit.begin()+1,unit.end(),[](double x){return x==0;});
    }
    Statement universalDeclaration() {
        const auto beginning=current();const auto declared=declaredTypeName();const auto explicitSchema=m_lastDeclaredSchema;
        if(m_options.languageVersion!="0.2")add("E1601","Algebra definitions/elements require language0.2.",beginning.span);
        const auto identifier=name();const std::string spelling(text(identifier));
        const bool protectedIdentifier=protectedName(identifier),duplicate=m_bindings.contains(spelling);
        if(protectedIdentifier)add("E1203","A keyword, basis or builtin cannot be declared.",identifier.span);
        if(duplicate)add("E1202","This identifier is already declared in the file.",identifier.span);
        expect("=");const auto errorsBefore=m_errors;auto value=initializer();const auto actual=check(value,false,false,false,declared);
        auto effective=explicitSchema?explicitSchema:value->schema;
        bool compatible=actual==declared;
        if(declared==Type::Element&&scalarType(actual)&&explicitSchema&&explicitSchema->algebra&&!explicitSchema->algebra->unit().empty())compatible=true;
        if(declared==Type::Element&&(!effective||!effective->algebra)){
            add("E2530","An element requires a known algebra; use element<A> or initialize with element(A,...).",value->span);compatible=false;
        }
        if(explicitSchema&&actual==Type::Element&&!compatibleSchema(explicitSchema,value->schema)){
            add("E2530","The initializer belongs to a different algebra definition.",value->span);compatible=false;
        }
        if(!compatible&&actual!=Type::Invalid)add("E1204","Cannot initialize "+std::string(typeName(declared))+" from "+std::string(typeName(actual))+".",value->span);
        const bool valid=compatible&&actual!=Type::Invalid&&errorsBefore==m_errors;
        if(!protectedIdentifier&&!duplicate){m_bindings.emplace(spelling,Binding{valid?declared:Type::Invalid,identifier.span,value->ode,value->function,effective});
            if(valid)m_result.symbols.push_back({spelling,declared,identifier.span,value->ode,value->function,effective});}
        Statement statement;statement.kind=StatementKind::Declaration;statement.name=spelling;statement.nameSpan=identifier.span;
        statement.declaredType=declared;statement.declaredSchema=effective;statement.expression=value;
        statement.span=terminate(beginning.span,value->span);return statement;
    }
    std::optional<Type> checkUniversalMember(const ExprPtr& value) {
        if(value->children.empty()||value->children.front()->type!=Type::Algebra)return {};
        const auto definition=universalDefinition(value->children.front());
        if(!definition){add("E2530","An algebra basis member requires a known immutable definition.",value->span);return Type::Invalid;}
        if(std::find(definition->basis().begin(),definition->basis().end(),value->text)==definition->basis().end()){
            add("E2530","Unknown basis name '"+value->text+"' in algebra "+definition->name()+".",value->span);return Type::Invalid;}
        value->schema=universalSchema(definition);return Type::Element;
    }
    std::optional<Type> checkUniversalCall(const ExprPtr& value) {
        const auto& name=value->text;
        if(name=="element"){const auto binding=m_bindings.find(name);if(binding!=m_bindings.end()&&binding->second.type==Type::Function)return {};}
        static const std::set<std::string> names{"builtin_algebra","import_algebra","element","algebra_info","algebra_basis","algebra_coefficients","algebra_leftop","algebra_rightop","algebra_solveleft","algebra_solveright","algebra_norm","algebra_json","algebra_products"};
        static const std::set<std::string> overloads{"coeff","conj","norm2","inv","commutator","associator","scalar","pure","solveleft","solveright","leftop","rightop","sin","cos","exp","sinh","cosh"};
        const bool seriesCoefficient=name=="coeff"&&!value->children.empty()&&value->children.front()->type==Type::ElementSeries;
        const bool overloaded=overloads.contains(name)&&!value->children.empty()&&(value->children.front()->type==Type::Element||seriesCoefficient);
        if(!names.contains(name)&&!overloaded)return {};
        bool valid=true;auto require=[&](bool condition,const std::string& message){if(!condition){add("E2530",message,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Native algebra definitions/elements require language0.2.");
        require(!hasNamedArguments(value),"Use positional algebra arguments and a trailing options record where documented.");
        const auto count=value->children.size();
        if(name=="builtin_algebra"||name=="import_algebra"){
            require(name=="builtin_algebra"?(count==1||count==2):(count>=1&&count<=3),"Use builtin_algebra(name[,options(p=3,q=0)]) or import_algebra(metadata_json[,products_csv_xlsx[,sheet]]).");
            if(count<1)return Type::Invalid;
            require(value->children[0]->kind==ExprKind::StringLiteral,"Algebra names/import paths must be literal strings so native types and dependencies are known.");
            if(!valid)return Type::Invalid;
            try{
                algebra::universal::DefinitionPtr definition;
                if(name=="builtin_algebra"){
                    algebra::universal::BuiltinOptions options;
                    if(count==2){const auto& settings=value->children[1];require(settings->kind==ExprKind::Call&&settings->text=="options","Builtin algebra options require literal options(p=...,q=...).");
                        if(valid){std::set<std::string> used;for(std::size_t i=0;i<settings->children.size();++i){const auto key=i<settings->argumentNames.size()?settings->argumentNames[i]:std::string();
                            const auto axis=key=="p"||key=="positive"?"p":key=="q"||key=="negative"?"q":"";
                            require(*axis&&used.insert(axis).second,"Only one literal p/positive and q/negative option is supported.");
                            const auto integer=universalIntegerLiteral(settings->children[i]);require(integer&&*integer>=0&&*integer<=6,"Clifford signature entries require integer literals0..6.");
                            if(integer&&*integer>=0&&*integer<=6){if(key=="p"||key=="positive")options.positive=static_cast<unsigned>(*integer);else options.negative=static_cast<unsigned>(*integer);}
                        }}
                    }
                    if(!valid)return Type::Invalid;
                    definition=algebra::universal::builtin(value->children[0]->text,options);
                }else{
                    const auto limit=std::min<std::size_t>(8*1024*1024,m_options.maximumImportBytes);
                    auto resolve=[&](const ExprPtr& argument){
                        require(argument->kind==ExprKind::StringLiteral,"Algebra import paths and optional XLSX sheet must be literal strings.");
                        const auto& spelling=argument->text;
                        require(!spelling.empty()&&spelling.size()<=4096&&spelling.find('\0')==std::string::npos,"Algebra import path is empty, contains NUL, or exceeds4096 UTF8 bytes.");
                        if(!valid)throw algebra::universal::Error("A4020","Invalid algebra table path.");
                        const std::u8string encoded(reinterpret_cast<const char8_t*>(spelling.data()),spelling.size());
                        auto path=std::filesystem::path(encoded);if(path.is_relative())path=m_options.sourceDirectory/path;
                        path=std::filesystem::weakly_canonical(std::filesystem::absolute(path));
                        if(!std::filesystem::is_regular_file(path))throw algebra::universal::Error("A4020","Algebra import requires a regular data file.");
                        return path;
                    };
                    auto read=[&](const std::filesystem::path& path){
                        if(std::filesystem::file_size(path)>limit)throw algebra::universal::Error("A4020","Algebra table exceeds the import-byte budget.");
                        std::ifstream file(path,std::ios::binary);if(!file)throw algebra::universal::Error("A4020","Cannot open algebra table data.");
                        std::string bytes;char buffer[8192];while(file){cancelled();file.read(buffer,sizeof(buffer));const auto received=static_cast<std::size_t>(file.gcount());if(received>limit-bytes.size())throw algebra::universal::Error("A4020","Algebra table exceeded the import-byte budget while reading.");bytes.append(buffer,received);}
                        if(!file.eof())throw algebra::universal::Error("A4020","Cannot read complete algebra table data.");
                        return bytes;
                    };
                    const auto path=resolve(value->children[0]);auto bytes=read(path);
                    std::vector<std::pair<std::filesystem::path,std::string>> captured;
                    algebra::universal::ImportOptions options;options.maxBytes=limit;options.maxIdentityOperations=100000;options.cancellation=m_options.cancellation;
                    if(count==1)definition=algebra::universal::fromJson(bytes,options);
                    else{
                        const auto dataPath=resolve(value->children[1]);auto dataBytes=read(dataPath);std::string sheet;
                        if(count==3){require(value->children[2]->kind==ExprKind::StringLiteral,"The XLSX sheet must be a literal string.");if(!valid)return Type::Invalid;sheet=value->children[2]->text;}
                        definition=algebraDefinitionFromTable(bytes,dataBytes,dataPath,sheet,options);
                        captured.emplace_back(dataPath,std::move(dataBytes));
                    }
                    captured.emplace_back(path,std::move(bytes));
                    for(auto& [capturedPath,capturedBytes]:captured){
                        const auto u8path=capturedPath.generic_u8string();const std::string identity(u8path.begin(),u8path.end());
                        if(m_importedIdentities.contains(identity)){
                            const auto previous=std::find_if(m_result.dependencies.begin(),m_result.dependencies.end(),[&](const auto& dependency){return dependency.identity==identity;});
                            if(previous==m_result.dependencies.end()||!previous->source||*previous->source!=capturedBytes){require(false,"Algebra table changed during analysis; save the file and run again.");return Type::Invalid;}
                        }else{
                            if(m_importBudget->bytes>m_options.maximumImportBytes||capturedBytes.size()>m_options.maximumImportBytes-m_importBudget->bytes){require(false,"Aggregate imported source/table bytes exceed the configured budget.");return Type::Invalid;}
                            m_importedIdentities.insert(identity);m_importBudget->bytes+=capturedBytes.size();m_result.dependencies.push_back({identity,std::make_shared<const std::string>(std::move(capturedBytes))});
                        }
                    }
                }
                value->schema=universalSchema(std::move(definition));return Type::Algebra;
            }catch(const algebra::universal::Error& error){cancelled();add("E2530",std::string("Invalid algebra definition: ")+error.what(),value->span);return Type::Invalid;}
            catch(const std::filesystem::filesystem_error& error){add("E2530",std::string("Algebra table path error: ")+error.what(),value->span);return Type::Invalid;}
        }
        const auto required=name=="element"?2u:name=="algebra_basis"||name=="coeff"||name=="commutator"||name=="solveleft"||name=="solveright"?2u:name=="associator"?3u:name=="algebra_solveleft"||name=="algebra_solveright"?2u:1u;
        require(count>=required,"Missing algebra argument; see help.");
        if(count<required)return Type::Invalid;
        const bool report=name=="algebra_solveleft"||name=="algebra_solveright";
        require(name=="element"||(report?(count==2||count==3):count==required),"Incorrect algebra argument count.");
        const auto definition=universalDefinition(value->children[0]);
        require(static_cast<bool>(definition),"This operation requires a known immutable algebra definition.");
        if(!definition)return Type::Invalid;
        if(name=="element"||name=="algebra_basis")require(value->children[0]->type==Type::Algebra,"Element construction/basis selection begins with an algebra definition.");
        else if(name!="algebra_info"&&name!="algebra_json"&&name!="algebra_products")require(value->children[0]->type==Type::Element||seriesCoefficient,"This operation requires an algebra element.");
        if(name=="algebra_info"||name=="algebra_json"||name=="algebra_products")require(value->children[0]->type==Type::Algebra||value->children[0]->type==Type::Element,"Expected an algebra definition or element.");
        if(name=="element"){
            if(count==2){const auto type=value->children[1]->type;require(scalarType(type)||type==Type::RealMatrix||type==Type::LogicalMatrix||rangeType(type),"Element coefficients require a finite real scalar/vector.");}
            else{require(count-1==definition->dimension(),"The coefficient count must equal the algebra dimension.");for(std::size_t i=1;i<count;++i)require(scalarType(value->children[i]->type),"Element constructor coefficients must be real scalars.");}
        }
        if(name=="coeff"||name=="algebra_basis"){
            require(value->children[1]->type==Type::Integer,"Basis/coefficient indices are zero-based integers.");
            if(const auto index=universalIntegerLiteral(value->children[1]))require(*index>=0&&static_cast<std::size_t>(*index)<definition->dimension(),"Algebra index lies outside0..dimension-1.");
        }
        if(name=="commutator"||name=="associator"||name=="solveleft"||name=="solveright"||report){
            const auto operands=name=="associator"?3u:2u;for(std::size_t i=1;i<operands;++i){const auto other=universalDefinition(value->children[i]);
                require(value->children[i]->type==Type::Element&&other&&other->hash()==definition->hash(),"All operands must belong to the same algebra definition.");}
        }
        if(report&&count==3)require(value->children[2]->type==Type::Record,"Equation solve options require a typed record.");
        if(name=="conj")require(!definition->conjugation().empty(),"This algebra has no declared conjugation map.");
        if(name=="norm2")require(!definition->metric().empty(),"norm2 requires an explicit algebra metric; algebra_norm gives the coordinate Euclidean norm.");
        if(name=="sin"||name=="cos"||name=="exp"||name=="sinh"||name=="cosh"){
            const auto& property=definition->properties().powerAssociative;
            require(!definition->unit().empty()&&property.value&&property.evidence==algebra::universal::Evidence::ProvedExact,"Whole-element elementary calculus requires a validated unit and exactly proved power associativity; declarations or numerical identity checks do not establish this capability.");
        }
        if(name=="scalar"||name=="pure")require(universalFirstUnit(*definition),"scalar/pure require the unit to be exactly the first basis vector; use explicit coefficients for a general unit.");
        if(!valid)return Type::Invalid;
        if(seriesCoefficient)return Type::RealSeries;
        if(name=="coeff"||name=="scalar"||name=="norm2"||name=="algebra_norm")return Type::Real;
        if(name=="algebra_json")return Type::String;
        if(name=="algebra_coefficients"||name=="algebra_leftop"||name=="algebra_rightop"||name=="leftop"||name=="rightop"||name=="algebra_products")return Type::RealMatrix;
        if(report){auto schema=std::make_shared<ValueSchema>();schema->fields={{"solution",Type::RealMatrix,{}},{"has_solution",Type::Integer,{}},{"status",Type::String,{}},{"rank",Type::Integer,{}},{"condition",Type::Real,{}},{"condition_available",Type::Integer,{}},{"relative_residual",Type::Real,{}},{"residual_available",Type::Integer,{}}};value->schema=schema;return Type::Record;}
        if(name=="algebra_info"){
            auto property=std::make_shared<ValueSchema>();property->fields={{"value",Type::Integer,{}},{"evidence",Type::String,{}},{"detail",Type::String,{}}};
            auto properties=std::make_shared<ValueSchema>();for(const auto* key:{"unital","commutative","associative","alternative","power_associative","conjugation","positive_metric","division"})properties->fields.push_back({key,Type::Record,property});
            auto schema=std::make_shared<ValueSchema>();for(const auto* key:{"format","name","id","hash","source_hash","builtin","convention","coefficient_domain","basis_labels"})schema->fields.push_back({key,Type::String,{}});
            for(const auto* key:{"dimension","terms","has_unit","has_conjugation","has_metric"})schema->fields.push_back({key,Type::Integer,{}});
            for(const auto* key:{"unit","conjugation","metric"})schema->fields.push_back({key,Type::RealMatrix,{}});
            schema->fields.push_back({"properties",Type::Record,properties});value->schema=schema;return Type::Record;
        }
        value->schema=universalSchema(definition);return Type::Element;
    }
