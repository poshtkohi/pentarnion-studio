    bool sameDataSchema(const Value& a,const Value& b,std::size_t depth=0)const {
        if(depth>32||valueType(a)!=valueType(b))return false;
        if(const auto* definition=std::get_if<UniversalDefinition>(&a))return (*definition)->hash()==std::get<UniversalDefinition>(b)->hash();
        if(const auto* element=std::get_if<UniversalElement>(&a))return element->definition()->hash()==std::get<UniversalElement>(b).definition()->hash();
        if(const auto* tensor=std::get_if<TensorValue>(&a))return detail::sameTensorDescriptor(**tensor,*std::get<TensorValue>(b));
        if(const auto* matrix=std::get_if<UniversalMatrix>(&a)){const auto& other=std::get<UniversalMatrix>(b);return (*matrix)->algebra->hash()==other->algebra->hash()&&(*matrix)->fixedShape==other->fixedShape;}
        if(const auto* matrix=std::get_if<MatrixValue>(&a))return (*matrix)->fixedShape==std::get<MatrixValue>(b)->fixedShape;
        if(const auto* series=std::get_if<ArraySeries>(&a)){const auto& other=std::get<ArraySeries>(b);if(series->valueType!=other.valueType||!series->schema||!other.schema||series->schema->shape!=other.schema->shape)return false;if(series->valueType==Type::Tensor&&series->schema->tensorEntryType!=other.schema->tensorEntryType)return false;return (series->valueType!=Type::ElementMatrix&&(series->valueType!=Type::Tensor||series->schema->tensorEntryType!=Type::Element))||(series->schema->algebra&&other.schema->algebra&&series->schema->algebra->hash()==other.schema->algebra->hash());}
        if(const auto* series=std::get_if<UniversalSeries>(&a)){const auto& other=std::get<UniversalSeries>(b);return series->complex==other.complex&&series->algebra->hash()==other.algebra->hash();}
        if(const auto* solution=std::get_if<SolutionValue>(&a)){
            const auto& other=std::get<SolutionValue>(b);if(solution->data->stateNames!=other.data->stateNames||solution->data->stateWidths!=other.data->stateWidths)return false;
            if(!solution->definition||!other.definition||solution->definition->states.size()!=other.definition->states.size())return false;
            for(std::size_t n=0;n<solution->definition->states.size();++n){const auto& x=solution->definition->states[n];const auto& y=other.definition->states[n];if(x.type!=y.type)return false;if(x.type==Type::Tensor&&(!x.schema||!y.schema||x.schema->shape!=y.schema->shape||x.schema->tensorEntryType!=y.schema->tensorEntryType||bool(x.schema->algebra)!=bool(y.schema->algebra)||(x.schema->algebra&&x.schema->algebra->hash()!=y.schema->algebra->hash())))return false;if((x.type==Type::RealMatrix||x.type==Type::PentarnionMatrix||x.type==Type::ElementMatrix)&&(!x.schema||!y.schema||x.schema->shape!=y.schema->shape))return false;if((x.type==Type::Element||x.type==Type::ElementMatrix)&&(!x.schema||!y.schema||!x.schema->algebra||!y.schema->algebra||x.schema->algebra->hash()!=y.schema->algebra->hash()))return false;}return true;
        }
        if(const auto* callable=std::get_if<FunctionValue>(&a)){
            const auto& other=std::get<FunctionValue>(b);if(callable->builtin!=other.builtin||!callable->definition||!other.definition)return callable->builtin==other.builtin&&!callable->definition&&!other.definition;
            const auto& first=*callable->definition;const auto& second=*other.definition;if(first.result!=second.result||first.results!=second.results||first.parameters.size()!=second.parameters.size())return false;
            auto schemaEqual=[&](const auto& left,const auto& right){if(!left||!right)return !left&&!right;if(left->shape!=right->shape||left->seriesElementType!=right->seriesElementType||left->tensorEntryType!=right->tensorEntryType)return false;return (!left->algebra&&!right->algebra)||(left->algebra&&right->algebra&&left->algebra->hash()==right->algebra->hash());};
            if(!schemaEqual(first.resultSchema,second.resultSchema))return false;
            if(first.resultSchema&&second.resultSchema){if(first.resultSchema->elements.size()!=second.resultSchema->elements.size())return false;for(std::size_t i=0;i<first.resultSchema->elements.size();++i){const auto& x=first.resultSchema->elements[i];const auto& y=second.resultSchema->elements[i];if(x.type!=y.type||!schemaEqual(x.schema,y.schema))return false;}}
            for(std::size_t i=0;i<first.parameters.size();++i){if(first.parameters[i].type!=second.parameters[i].type||first.parameters[i].name!=second.parameters[i].name)return false;const auto x=i<first.parameterSchemas.size()?first.parameterSchemas[i]:first.parameters[i].schema;const auto y=i<second.parameterSchemas.size()?second.parameterSchemas[i]:second.parameters[i].schema;if(!schemaEqual(x,y))return false;}return true;
        }
        if(const auto* record=std::get_if<RecordValue>(&a)){const auto& other=std::get<RecordValue>(b)->fields;if((*record)->fields.size()!=other.size())return false;for(const auto& [name,item]:(*record)->fields){const auto found=other.find(name);if(found==other.end()||!sameDataSchema(item,found->second,depth+1))return false;}}
        if(const auto* tuple=std::get_if<TupleValue>(&a)){const auto& other=std::get<TupleValue>(b)->values;if((*tuple)->values.size()!=other.size())return false;for(std::size_t n=0;n<other.size();++n)if(!sameDataSchema((*tuple)->values[n],other[n],depth+1))return false;}
        return true;
    }
    const std::map<std::string,Value>* recordFields(const Value& value)const {
        const auto* record=std::get_if<RecordValue>(&value);return record?&(*record)->fields:nullptr;
    }
    Value makeRecord(std::map<std::string,Value> fields,SourceSpan span) {
        if(fields.size()>128)fail("R1951","A generated report is limited to 128 fields.",span);
        auto value=std::make_shared<RecordData>();value->fields=std::move(fields);return RecordValue(value);
    }
    Value makeTuple(std::vector<Value> values,SourceSpan span) {
        if(values.empty()||values.size()>16)fail("R1951","A result tuple requires 1..16 values.",span);
        auto tuple=std::make_shared<TupleData>();tuple->values=std::move(values);return TupleValue(tuple);
    }
    void checkFoundationWorkspace(const std::string& target,const Value& candidate,SourceSpan span) {
        std::size_t coordinates=0,stringBytes=0,solutionCoordinates=0,retainedSolutionCoordinates=0,algebraBytes=0,originBytes=0,tensorBytes=0;std::unordered_set<const void*> unique;
        auto inspectOrigin=[&](const std::shared_ptr<const language::SourceOrigin>& origin){if(!origin||!origin->source||!unique.insert(origin->source.get()).second)return;constexpr std::size_t limit=32*1024*1024;if(origin->source->size()>limit-originBytes)fail("R1005","Retained function/model source snapshots exceed32 MiB.",span);originBytes+=origin->source->size();};
        const auto coordinateLimit=std::min<std::size_t>(8000000,options_.maxMatrixCoordinates);
        auto chargeCoordinates=[&](std::size_t count){if(count>coordinateLimit-coordinates)fail("R1902","Nested arrays and algebra elements exceed the retained-coordinate budget.",span);coordinates+=count;};
        auto inspectAlgebra=[&](const UniversalDefinition& definition){if(!definition||!unique.insert(definition.get()).second)return;tick(span);
            // Includes native tables, vector capacity, exact coefficients,
            // metadata and both canonical/original source byte snapshots.
            const auto count=definition->retainedBytes();
            constexpr std::size_t limit=64*1024*1024;if(count>limit-algebraBytes)fail("R1005","Retained algebra definitions exceed 64 MiB.",span);algebraBytes+=count;};
        std::function<void(const std::shared_ptr<const language::ValueSchema>&,std::size_t)> inspectSchema;
        std::function<void(const std::shared_ptr<const language::FunctionDefinition>&,std::size_t)> inspectFunction;
        std::function<void(const std::shared_ptr<const language::OdeDefinition>&,std::size_t)> inspectModel;
        std::function<void(const language::ExprPtr&,std::size_t)> inspectExpression;
        std::function<void(const language::Statement&,std::size_t)> inspectStatement;
        // Traversing immutable checked metadata is storage accounting, not an
        // executed language instruction. Do not consume legacy VM quotas.
        auto checkDepth=[&](std::size_t depth){if(depth>512)fail("R1951","Retained typed definitions exceed the nesting budget.",span);};
        inspectSchema=[&](const auto& schema,std::size_t depth){if(!schema||!unique.insert(schema.get()).second)return;checkDepth(depth);inspectAlgebra(schema->algebra);for(const auto& field:schema->fields)inspectSchema(field.schema,depth+1);for(const auto& field:schema->elements)inspectSchema(field.schema,depth+1);inspectFunction(schema->function,depth+1);};
        inspectExpression=[&](const auto& expression,std::size_t depth){if(!expression||!unique.insert(expression.get()).second)return;checkDepth(depth);inspectSchema(expression->schema,depth+1);inspectFunction(expression->function,depth+1);inspectModel(expression->ode,depth+1);for(const auto& child:expression->children)inspectExpression(child,depth+1);};
        inspectStatement=[&](const auto& statement,std::size_t depth){checkDepth(depth);inspectSchema(statement.declaredSchema,depth+1);inspectFunction(statement.function,depth+1);inspectModel(statement.ode,depth+1);inspectExpression(statement.expression,depth+1);for(const auto& argument:statement.arguments)inspectExpression(argument,depth+1);for(const auto& child:statement.body)inspectStatement(child,depth+1);for(const auto& child:statement.alternative)inspectStatement(child,depth+1);};
        inspectFunction=[&](const auto& function,std::size_t depth){if(!function||!unique.insert(function.get()).second)return;checkDepth(depth);inspectOrigin(function->origin);inspectSchema(function->resultSchema,depth+1);for(const auto& schema:function->parameterSchemas)inspectSchema(schema,depth+1);for(const auto& parameter:function->parameters)inspectSchema(parameter.schema,depth+1);for(const auto& expression:function->parameterDefaults)inspectExpression(expression,depth+1);for(const auto& statement:function->body)inspectStatement(statement,depth+1);};
        inspectModel=[&](const auto& model,std::size_t depth){if(!model||!unique.insert(model.get()).second)return;checkDepth(depth);inspectOrigin(model->origin);for(const auto& state:model->states)inspectSchema(state.schema,depth+1);for(const auto& parameter:model->parameters)inspectSchema(parameter.schema,depth+1);for(const auto& expression:model->derivatives)inspectExpression(expression,depth+1);};
        std::function<void(const Value&,std::size_t)> inspect=[&](const Value& value,std::size_t depth){
            tick(span);if(depth>32)fail("R1951","Nested values exceed the depth budget.",span);
            if(const auto* text=std::get_if<std::string>(&value)){if(text->size()>8*1024*1024-stringBytes)fail("R1951","Retained string data exceeds 8 MiB.",span);stringBytes+=text->size();}
            else if(const auto* definition=std::get_if<UniversalDefinition>(&value))inspectAlgebra(*definition);
            else if(const auto* element=std::get_if<UniversalElement>(&value)){inspectAlgebra(element->definition());chargeCoordinates(element->coefficients().size());}
            else if(const auto* tensor=std::get_if<TensorValue>(&value)){if(!unique.insert(tensor->get()).second)return;inspectAlgebra((*tensor)->algebra);chargeCoordinates((*tensor)->values.capacity());constexpr std::size_t limit=1024ull*1024*1024;const auto retained=(*tensor)->retainedBytes();if(retained>limit-tensorBytes)fail("R1005","Nested typed tensors exceed their retained memory budget.",span);tensorBytes+=retained;}
            else if(const auto* array=std::get_if<UniversalMatrix>(&value)){if(!unique.insert(array->get()).second)return;inspectAlgebra((*array)->algebra);chargeCoordinates((*array)->values.size());}
            else if(const auto* series=std::get_if<ArraySeries>(&value)){inspectSchema(series->schema,0);const auto* solution=series->data.get();if(!unique.insert(solution).second)return;const auto limit=std::min<std::size_t>(8000000,options_.maxSolutionCoordinates);if(solution->values.size()>limit-solutionCoordinates)fail("R1005","Nested array-state series exceed the retained sample-coordinate budget.",span);solutionCoordinates+=solution->values.size();const auto retainedLimit=std::min<std::size_t>(32000000,options_.maxRetainedSolutionCoordinates);if(solution->retainedCoordinateCount()>retainedLimit-retainedSolutionCoordinates)fail("R1005","Nested array-state series exceed the auxiliary storage budget.",span);retainedSolutionCoordinates+=solution->retainedCoordinateCount();}
            else if(const auto* model=std::get_if<ModelValue>(&value))inspectModel(model->definition,0);
            else if(const auto* array=std::get_if<MatrixValue>(&value)){if(!unique.insert(array->get()).second)return;chargeCoordinates((*array)->values.size()*((*array)->pentarnion?5:1));}
            else if(std::holds_alternative<SolutionValue>(value)||std::holds_alternative<RealSeries>(value)||std::holds_alternative<PentSeries>(value)||std::holds_alternative<UniversalSeries>(value)){const ode::Solution* solution=nullptr;if(const auto* item=std::get_if<SolutionValue>(&value)){solution=item->data.get();inspectModel(item->definition,0);}else if(const auto* item=std::get_if<RealSeries>(&value))solution=item->data.get();else if(const auto* item=std::get_if<PentSeries>(&value))solution=item->data.get();else {const auto& series=std::get<UniversalSeries>(value);solution=series.data.get();inspectAlgebra(series.algebra);}if(!unique.insert(solution).second)return;const auto limit=std::min<std::size_t>(8000000,options_.maxSolutionCoordinates);if(solution->values.size()>limit-solutionCoordinates)fail("R1005","Nested solution states exceed the sample-coordinate budget.",span);solutionCoordinates+=solution->values.size();const auto retainedLimit=std::min<std::size_t>(32000000,options_.maxRetainedSolutionCoordinates);if(solution->retainedCoordinateCount()>retainedLimit-retainedSolutionCoordinates)fail("R1005","Nested solutions exceed the auxiliary storage budget.",span);retainedSolutionCoordinates+=solution->retainedCoordinateCount();}
            else if(const auto* record=std::get_if<RecordValue>(&value)){if(!unique.insert(record->get()).second)return;for(const auto& [name,item]:(*record)->fields){(void)name;inspect(item,depth+1);}}
            else if(const auto* tuple=std::get_if<TupleValue>(&value)){if(!unique.insert(tuple->get()).second)return;for(const auto& item:(*tuple)->values)inspect(item,depth+1);}
            else if(const auto* callable=std::get_if<FunctionValue>(&value)){inspectFunction(callable->definition,0);if(callable->captures&&unique.insert(callable->captures.get()).second)for(const auto& [name,item]:callable->captures->values){(void)name;inspect(item,depth+1);}}
        };
        inspect(candidate,0);for(const auto& [name,value]:variables_)if(name!=target)inspect(value,0);
        for(const auto* frame:outerFrames_)for(const auto& [name,value]:*frame){(void)name;inspect(value,0);}
    }
    Value evaluateHandle(const Expr& expression) {
        if(const auto found=variables_.find(expression.text);found!=variables_.end()&&std::holds_alternative<FunctionValue>(found->second)){
            FunctionValue value=std::get<FunctionValue>(found->second);
            if(!value.captures){auto captured=std::make_shared<CapturedEnvironment>();for(const auto& [name,item]:variables_){tick(expression.span);if(std::holds_alternative<FunctionValue>(item))captured->values.emplace(name,item);}value.captures=captured;}
            return value;
        }
        if(expression.function)return FunctionValue{expression.function,{},expression.text};
        fail("R1951","The named function is not available in this workspace.",expression.span);
    }
    Value evaluateLambda(const Expr& expression) {
        auto captures=std::make_shared<CapturedEnvironment>();
        for(const auto& name:expression.function->captureNames){tick(expression.span);const auto found=variables_.find(name);if(found==variables_.end())fail("R1951","An anonymous capture is no longer available: "+name,expression.span);captures->values.emplace(name,found->second);}
        return FunctionValue{expression.function,captures,{}};
    }
    Value invokeCallable(const Value& value,const std::vector<Value>& arguments,SourceSpan span) {
        const auto* callable=std::get_if<FunctionValue>(&value);if(!callable)fail("R1951","A function handle was required.",span);
        if(!callable->builtin.empty()){Expr expression;expression.kind=ExprKind::Call;expression.text=callable->builtin;expression.span=span;return call(expression,arguments);}
        return callFunction(*callable,arguments,span);
    }
    void runDestructure(const language::Statement& statement) {
        const Value value=evaluate(*statement.expression);const auto* tuple=std::get_if<TupleValue>(&value);
        if(!tuple||(*tuple)->values.size()!=statement.names.size())fail("R1951","The returned output count does not match its targets.",statement.span);
        Environment previous=variables_;bool committed=false;struct Restore{Environment& current;Environment& previous;bool& committed;~Restore(){if(!committed)current=std::move(previous);}} restore{variables_,previous,committed};
        for(std::size_t n=0;n<statement.names.size();++n){const auto& name=statement.names[n];if(!variables_.contains(name)&&variables_.size()>=options_.maxWorkspaceVariables)fail("R1005","Output targets exceed the workspace variable limit.",statement.span);
            std::shared_ptr<const language::ValueSchema> schema;if(statement.declaredSchema&&n<statement.declaredSchema->elements.size())schema=statement.declaredSchema->elements[n].schema;else if(const auto found=variables_.find(name);found!=variables_.end())schema=runtimeSchema(found->second);
            Value output=convert((*tuple)->values[n],statement.declaredTypes[n],statement.span,schema);checkMatrixWorkspace(name,output,statement.span);checkFoundationWorkspace(name,output,statement.span);variables_.insert_or_assign(name,std::move(output));}
        committed=true;lastValue_.reset();lastName_.clear();
    }
    void runImport(const language::Statement& statement) {
        if(!statement.module)fail("R1950","Checked module source was unavailable.",statement.span);
        if(variables_.contains(statement.name)||variables_.size()>=options_.maxWorkspaceVariables)fail("R1950","Import namespace cannot replace an existing binding.",statement.span);
        Environment outer=std::move(variables_);variables_.clear();
        outerFrames_.push_back(&outer);bool registered=true;
        auto restore=[&]{if(registered){outerFrames_.pop_back();registered=false;variables_=std::move(outer);}};
        try{
            runStatements(statement.module->statements);auto exports=variables_;
            auto captured=std::make_shared<CapturedEnvironment>();for(const auto& [name,value]:exports)if(std::holds_alternative<FunctionValue>(value))captured->values.emplace(name,value);
            for(auto& [name,value]:exports){(void)name;if(auto* callable=std::get_if<FunctionValue>(&value);callable&&!callable->captures)callable->captures=captured;}
            const auto record=makeRecord(std::move(exports),statement.span);restore();checkFoundationWorkspace(statement.name,record,statement.span);variables_.emplace(statement.name,record);lastValue_.reset();lastName_.clear();
        }catch(const Failure& error){restore();fail(error.code,"In imported module "+statement.moduleId+" (imported at this statement): "+error.what(),statement.span);}
        catch(...){restore();throw;}
    }
    std::complex<double> complexValue(const Value& value,SourceSpan span)const {
        if(const auto* number=std::get_if<std::complex<double>>(&value))return *number;
        return {real(value,span),0};
    }
    std::complex<double> finiteComplex(std::complex<double> value,SourceSpan span)const {
        finite(value.real(),span);finite(value.imag(),span);return value;
    }
    std::optional<Value> foundationUnary(const Expr& expression,const Value& value) {
        if(const auto* number=std::get_if<std::complex<double>>(&value)){
            if(expression.text=="-")return Value(-*number);
            if(expression.text=="+")return value;
            return {};
        }
        if((expression.text=="!"||expression.text=="~")&&std::holds_alternative<MatrixValue>(value)){
            const auto& source=*std::get<MatrixValue>(value);if(source.pentarnion)fail("R1951","Logical arrays require real entries.",expression.span);
            auto result=newMatrix(source.rows,source.columns,false,expression.span);result->logical=true;
            for(const auto& item:source.values){tick(expression.span);result->values.push_back(P::scalar(context_,item.coefficients()[0]==0?1:0));}return Value(MatrixValue(result));
        }
        return {};
    }
    std::optional<Value> foundationBinary(const Expr& expression,const Value& left,const Value& right) {
        const auto& op=expression.text;const auto span=expression.span;
        if(std::holds_alternative<std::complex<double>>(left)||std::holds_alternative<std::complex<double>>(right)){
            const auto a=complexValue(left,span),b=complexValue(right,span);
            if(op=="=="||op=="!="||op=="~=")return Value(I((op=="==")== (a==b)));
            if(op=="+")return Value(finiteComplex(a+b,span));
            if(op=="-")return Value(finiteComplex(a-b,span));
            if(op=="*"||op==".*")return Value(finiteComplex(a*b,span));
            if(op=="/"||op=="./"){if(b==std::complex<double>{})fail("R1003","Complex division by zero.",span);return Value(finiteComplex(a/b,span));}
            if(op=="^"||op==".^")return Value(finiteComplex(std::pow(a,b),span));
            fail("R1951","Unsupported complex operator.",span);
        }
        const bool ordered=op=="<"||op==">"||op=="<="||op==">="||op=="&"||op=="|";
        if(ordered&&(std::holds_alternative<MatrixValue>(left)||std::holds_alternative<MatrixValue>(right))){
            const auto* a=std::get_if<MatrixValue>(&left);const auto* b=std::get_if<MatrixValue>(&right);
            const auto rows=a?(*a)->rows:(*b)->rows,columns=a?(*a)->columns:(*b)->columns;
            if((a&&(*a)->pentarnion)||(b&&(*b)->pentarnion)||(a&&b&&((*a)->rows!=(*b)->rows||(*a)->columns!=(*b)->columns)))fail("R1951","Logical array operands need compatible real shapes.",span);
            auto result=newMatrix(rows,columns,false,span);result->logical=true;
            for(std::size_t n=0;n<rows*columns;++n){tick(span);const double x=a?(*a)->values[n].coefficients()[0]:real(left,span),y=b?(*b)->values[n].coefficients()[0]:real(right,span);
                const bool value=op=="<"?x<y:op==">"?x>y:op=="<="?x<=y:op==">="?x>=y:op=="&"?(x!=0&&y!=0):(x!=0||y!=0);result->values.push_back(P::scalar(context_,value?1:0));}
            return Value(MatrixValue(result));
        }
        if(op=="&"||op=="|")return Value(I(op=="&"?(truth(left,span)&&truth(right,span)):(truth(left,span)||truth(right,span))));
        return {};
    }
    std::optional<Value> foundationCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(name=="options"||name=="with_options"){
            std::map<std::string,Value> fields;const auto start=name=="options"?0u:1u;
            if(start){const auto* source=recordFields(arguments.at(0));if(!source)fail("R1952","Defaults must be an options record.",span);fields=*source;}
            for(std::size_t n=start;n<arguments.size();++n){tick(span);const auto& key=expression.argumentNames.at(n);if(key.empty())fail("R1952","Options require named fields.",span);
                if(start){const auto found=fields.find(key);if(found==fields.end())fail("R1952","Unknown option: "+key,span);found->second=convert(arguments[n],valueType(found->second),span);}
                else if(!fields.emplace(key,arguments[n]).second)fail("R1952","Duplicate option: "+key,span);}
            return makeRecord(std::move(fields),span);
        }
        if(name=="get_option"){
            const auto* fields=recordFields(arguments.at(0));if(!fields)fail("R1952","An options record was required.",span);
            const auto found=fields->find(std::get<std::string>(arguments.at(1)));if(found==fields->end())fail("R1952","Unknown option field.",span);return found->second;
        }
        if(name=="complex")return Value(std::complex<double>(real(arguments.at(0),span),arguments.size()==2?real(arguments[1],span):0));
        if(name=="realpart"||name=="imagpart"||name=="arg"){
            const auto number=complexValue(arguments.at(0),span);return Value(name=="realpart"?number.real():name=="imagpart"?number.imag():std::arg(number));
        }
        if(arguments.size()==1&&std::holds_alternative<std::complex<double>>(arguments[0])){
            const auto number=std::get<std::complex<double>>(arguments[0]);std::complex<double> result;
            if(name=="abs")return Value(finite(std::abs(number),span));
            if(name=="norm2")return Value(finite(std::norm(number),span));
            if(name=="conj")result=std::conj(number);else if(name=="sqrt")result=std::sqrt(number);else if(name=="sin")result=std::sin(number);else if(name=="cos")result=std::cos(number);else if(name=="exp")result=std::exp(number);else if(name=="sinh")result=std::sinh(number);else if(name=="cosh")result=std::cosh(number);else if(name=="tan")result=std::tan(number);else if(name=="asin")result=std::asin(number);else if(name=="acos")result=std::acos(number);else if(name=="atan")result=std::atan(number);else if(name=="tanh")result=std::tanh(number);else if(name=="asinh")result=std::asinh(number);else if(name=="acosh")result=std::acosh(number);else if(name=="atanh")result=std::atanh(number);else if(name=="log")result=std::log(number);else if(name=="log10")result=std::log10(number);else if(name=="log2")result=std::log(number)/std::log(2.0);else return {};
            return Value(finiteComplex(result,span));
        }
        if(name=="hypot")return Value(finite(std::hypot(real(arguments.at(0),span),real(arguments.at(1),span)),span));
        if(name=="expm1"||name=="log1p"||name=="erf"||name=="erfc"||name=="sign"){
            auto apply=[&](double x){if(name=="log1p"&&x<=-1)fail("R1001","log1p requires real values greater than -1.",span);return finite(name=="expm1"?std::expm1(x):name=="log1p"?std::log1p(x):name=="erf"?std::erf(x):name=="erfc"?std::erfc(x):(x>0?1.0:x<0?-1.0:0.0),span);};
            if(const auto* array=std::get_if<MatrixValue>(&arguments.at(0))){if((*array)->pentarnion)fail("R1951","This function requires real array values.",span);auto result=newMatrix((*array)->rows,(*array)->columns,false,span);for(const auto& entry:(*array)->values){tick(span);result->values.push_back(P::scalar(context_,apply(entry.coefficients()[0])));}return Value(MatrixValue(result));}
            return Value(apply(real(arguments.at(0),span)));
        }
        return {};
    }
