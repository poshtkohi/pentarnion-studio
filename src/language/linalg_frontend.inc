    std::optional<Type> checkLinalgCall(const ExprPtr& value) {
        const auto& name=value->text;const auto count=value->children.size();
        const bool solve=name=="linsolve"||name=="solve_report"||name=="solveleft"||name=="solveright";
        const bool sparseOperation=name=="full"||name=="nnz"||name=="sparsemul"||name=="sparsetranspose"||name=="sparsesolve"||name=="sparse_solve_report";
        const bool decomposition=name=="det"||name=="inv"||name=="lu"||name=="qr"||name=="chol"||name=="svd"||name=="eig"||name=="schur"||name=="rank"||name=="rcond"||name=="cond";
        const bool op=name=="leftop"||name=="rightop";
        if(!solve&&!sparseOperation&&!decomposition&&!op&&name!="matmul"&&name!="batchmul"&&name!="sparse")return {};
        // The original scalar inverse retains its algebra semantics and arity.
        if(name=="inv"&&(count==0||!matrixType(value->children[0]->type)))return {};
        bool valid=true;
        auto reject=[&](const std::string& message){add("E1961",message,value->span);valid=false;};
        auto arity=[&](std::size_t base,bool options=true){if(count!=base&&(!options||count!=base+1))reject("Incorrect linear algebra argument count; optional settings use a final options record.");if(count==base+1&&value->children.back()->type!=Type::Record&&value->children.back()->type!=Type::Invalid)reject("Linear algebra settings require an options record.");};
        if(m_options.languageVersion!="0.2")reject("Advanced linear algebra requires language 0.2.");
        if(hasNamedArguments(value))reject("Use positional matrix arguments and a final typed options record.");
        auto inputType=[&](std::size_t index){return index<count?value->children[index]->type:Type::Invalid;};
        auto array=[&](std::size_t index,bool scalar=false){const auto type=inputType(index);if(type!=Type::Invalid&&!matrixType(type)&&!rangeType(type)&&!(scalar&&(scalarType(type)||type==Type::Pentarnion)))reject("This linear algebra argument must be a numeric matrix or range.");};
        auto realArray=[&](std::size_t index,bool scalar=false){array(index,scalar);if(pentarnionType(inputType(index)))reject("Real decomposition/solve required; use explicit leftop/rightop and solveleft/solveright for Pentarnion operators.");};
        auto promoted=[&](Type a,Type b){return pentarnionType(a)||pentarnionType(b)?Type::PentarnionMatrix:Type::RealMatrix;};
        auto fieldType=[](const std::shared_ptr<const ValueSchema>& schema,std::string_view field){if(schema)for(const auto& item:schema->fields)if(item.name==field)return item.type;return Type::Invalid;};
        auto sparseElement=[&](std::size_t index){return index<count&&fieldType(value->children[index]->schema,"values")==Type::PentarnionMatrix?Type::PentarnionMatrix:Type::RealMatrix;};
        auto sparseSchema=[](Type entries){auto schema=std::make_shared<ValueSchema>();schema->fields={{"format",Type::String,{}},{"rows",Type::Integer,{}},{"columns",Type::Integer,{}},{"row_indices",Type::RealMatrix,{}},{"column_pointers",Type::RealMatrix,{}},{"values",entries,{}}};return schema;};
        auto record=[&](std::size_t index){if(inputType(index)!=Type::Record&&inputType(index)!=Type::Invalid)reject("Sparse operations require a canonical sparse record.");};
        Type result=Type::RealMatrix;
        if(name=="matmul") {arity(2);array(0);array(1);result=promoted(inputType(0),inputType(1));}
        else if(name=="batchmul") {
            arity(2);if(inputType(0)!=Type::Tuple||inputType(1)!=Type::Tuple)reject("batchmul requires equal tuples of 1..16 numeric matrices.");
            auto schema=std::make_shared<ValueSchema>();
            if(count>=2&&value->children[0]->schema&&value->children[1]->schema){const auto& a=value->children[0]->schema->elements;const auto& b=value->children[1]->schema->elements;
                if(a.empty()||a.size()>16||a.size()!=b.size())reject("batchmul tuple lengths must match and be in 1..16.");
                for(std::size_t i=0;i<std::min(a.size(),b.size());++i){if((!matrixType(a[i].type)&&!rangeType(a[i].type))||(!matrixType(b[i].type)&&!rangeType(b[i].type)))reject("Each batch entry must be a numeric matrix.");schema->elements.push_back({{},promoted(a[i].type,b[i].type),{}});}
            }
            value->schema=std::move(schema);result=Type::Tuple;
        }else if(name=="sparse") {
            if(count!=1&&count!=5)reject("Use sparse(A) or sparse(row_indices,column_indices,values,rows,columns).");
            Type entries=Type::RealMatrix;
            if(count==1){array(0);entries=promoted(inputType(0),Type::RealMatrix);}
            if(count==5){realArray(0);realArray(1);array(2);entries=promoted(inputType(2),Type::RealMatrix);for(std::size_t i=3;i<5;++i)if(inputType(i)!=Type::Integer&&inputType(i)!=Type::Invalid)reject("Sparse dimensions require Integer type.");}
            value->schema=sparseSchema(entries);result=Type::Record;
        }else if(sparseOperation) {
            const bool binary=name=="sparsemul"||name=="sparsesolve"||name=="sparse_solve_report";
            arity(binary?2:1,name!="full"&&name!="nnz");record(0);
            if(name=="nnz")result=Type::Integer;
            else if(name=="full")result=sparseElement(0);
            else if(name=="sparsetranspose"){value->schema=sparseSchema(sparseElement(0));result=Type::Record;}
            else if(name=="sparsemul"){
                if(inputType(1)==Type::Record){value->schema=sparseSchema(promoted(sparseElement(0),sparseElement(1)));result=Type::Record;}
                else {array(1);result=promoted(sparseElement(0),inputType(1));}
            }else {
                realArray(1);if(sparseElement(0)==Type::PentarnionMatrix)reject("Sparse equation solving uses real entries; construct an explicit real-coordinate left/right operator.");
                result=name=="sparse_solve_report"?Type::Record:Type::RealMatrix;
            }
        }else if(solve) {
            arity(2);array(0);array(1);
            if(name=="linsolve"||name=="solve_report"){realArray(0);realArray(1);result=name=="solve_report"?Type::Record:Type::RealMatrix;}
            else result=promoted(inputType(0),inputType(1));
        }else if(op){arity(1);array(0,true);result=Type::RealMatrix;}
        else {
            arity(1);realArray(0,name=="det");
            if(name=="det"||name=="rcond"||name=="cond")result=Type::Real;
            else if(name=="rank")result=Type::Integer;
            else if(name=="inv"||name=="chol")result=Type::RealMatrix;
            else {
                auto schema=std::make_shared<ValueSchema>();
                schema->fields={{"rank",Type::Integer,{}},{"rcond",Type::Real,{}},{"backend_threads",Type::Integer,{}}};
                auto matrixFields=[&](std::initializer_list<const char*> names){for(const auto field:names)schema->fields.push_back({field,Type::RealMatrix,{}});};
                if(name=="lu")matrixFields({"L","U","P"});
                else if(name=="qr")matrixFields({"Q","R","P"});
                else if(name=="svd")matrixFields({"U","S","V","singular_values"});
                else if(name=="eig")matrixFields({"vectors_real","vectors_imag","values_real","values_imag"});
                else matrixFields({"Q","T"});
                value->schema=std::move(schema);result=Type::Record;
            }
        }
        if(name=="solve_report"||name=="sparse_solve_report"){
            auto schema=std::make_shared<ValueSchema>();schema->fields={{"solution",Type::RealMatrix,{}},{"residual_norm",Type::Real,{}},{"relative_residual",Type::Real,{}},{"rank",Type::Integer,{}},{"rank_available",Type::Integer,{}},{"rcond",Type::Real,{}},{"rcond_available",Type::Integer,{}},{"iterations",Type::Integer,{}},{"error_estimate",Type::Real,{}},{"converged",Type::Integer,{}},{"backend_threads",Type::Integer,{}},{"method",Type::String,{}}};value->schema=std::move(schema);
        }
        return valid?result:Type::Invalid;
    }
