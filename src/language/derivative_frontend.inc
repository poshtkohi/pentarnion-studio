    std::shared_ptr<const ValueSchema> nativeSolverParameterSchema(const FunctionDefinition& function,std::size_t n) const {
        return n<function.parameterSchemas.size()?function.parameterSchemas[n]:n<function.parameters.size()?function.parameters[n].schema:nullptr;
    }
    bool nativeSolverScalar(Type type,const std::shared_ptr<const ValueSchema>& schema={}) const {
        return type!=Type::Integer&&coordinateWidth(type,schema)>0;
    }
    bool checkNativeSolverPoint(const ExprPtr& point,const FunctionDefinition& function,const char* code) {
        bool valid=true;
        auto item=[&](Type type,const std::shared_ptr<const ValueSchema>& schema,std::size_t n){
            if(n>=function.parameters.size())return;
            const auto target=function.parameters[n].type;const auto expected=nativeSolverParameterSchema(function,n);
            if(target==Type::Tensor){
                if(type!=Type::Tensor||!schema||!expected||schema->shape!=expected->shape||schema->tensorEntryType!=expected->tensorEntryType||bool(schema->algebra)!=bool(expected->algebra)||(schema->algebra&&schema->algebra->hash()!=expected->algebra->hash())){add(code,"A native tensor point must match its complete rank, shape, entry type and immutable algebra definition.",point->span);valid=false;}
                return;
            }
            if(type==Type::Tensor){add(code,"A typed tensor point requires a declared tensor parameter; use explicit real coordinates for a deliberate flattened view.",point->span);valid=false;return;}
            if(universalArrayType(target)){
                if(!universalArrayType(type)){add(code,"A fixed-array native parameter requires an array point with its complete declared shape.",point->span);valid=false;return;}
                const auto actual=n==0?universalExpressionShape(point):schema?schema->shape:std::vector<std::size_t>{};if(expected&&!expected->shape.empty()&&!actual.empty()&&actual!=expected->shape){add(code,"Native array point shape differs from the compiled parameter contract.",point->span);valid=false;}
                if(target==Type::ElementMatrix){if(type==Type::PentarnionMatrix||(type==Type::ElementMatrix&&(!schema||!schema->algebra||!expected||!expected->algebra||schema->algebra->hash()!=expected->algebra->hash()))){add(code,"Native array point entry algebra differs from its parameter.",point->span);valid=false;}if(type==Type::RealMatrix&&(!expected||!expected->algebra||expected->algebra->unit().empty())){add(code,"Embedding real array entries requires an algebra unit.",point->span);valid=false;}}
                else if(type==Type::ElementMatrix||(target==Type::RealMatrix&&type==Type::PentarnionMatrix)){add(code,"Native array conversion cannot discard entry coefficients.",point->span);valid=false;}
                return;
            }
            if(type==Type::Element&&(target!=Type::Element||!compatibleSchema(schema,expected))){add(code,"Native solver point belongs to a different algebra definition from its declared parameter.",point->span);valid=false;}
            else if(target==Type::Element&&type!=Type::Element&&type!=Type::Invalid&&(!scalarType(type)||!expected||!expected->algebra||expected->algebra->unit().empty())){add(code,"Native solver algebra parameter requires its matching element or a real scalar embedded through its declared unit.",point->span);valid=false;}
            else if(type==Type::Complex&&target!=Type::Complex){add(code,"Native solver complex coordinates require a declared complex parameter.",point->span);valid=false;}
        };
        if(point->type==Type::Tuple&&point->schema){for(std::size_t n=0;n<point->schema->elements.size();++n)item(point->schema->elements[n].type,point->schema->elements[n].schema,n);}
        else if((!function.parameters.empty()&&universalArrayType(function.parameters.front().type))||point->type==Type::ElementMatrix||point->type==Type::Tensor||(!matrixType(point->type)&&!rangeType(point->type)))item(point->type,point->schema,0);
        return valid;
    }
#include "dependency_pattern_frontend.inc"
    std::optional<Type> checkDerivativeCall(const ExprPtr& value) {
        if(auto pattern=checkDependencyPatternCall(value))return pattern;
        const auto& name=value->text;const auto count=value->children.size();
        const bool product=name=="jvp"||name=="vjp"||name=="hvp";
        if(name!="jacobian"&&name!="hessian"&&name!="derivative_check"&&!product&&name!="gradient")return {};
        if(name=="gradient"&&(count==0||value->children[0]->type!=Type::Function))return {};
        bool valid=true;auto reject=[&](std::string message){add("E2101",std::move(message),value->span);valid=false;};
        if(m_options.languageVersion!="0.2")reject("Native derivatives require language0.2.");
        const std::size_t required=product?3:2;
        if(count<required||count>required+1)reject("Use derivative(function,point[,options]) or product(function,point,direction[,options]).");
        if(count&&value->children[0]->type!=Type::Function&&value->children[0]->type!=Type::Invalid)reject("Derivative first argument must be a typed function handle.");
        if(count>1){const auto type=value->children[1]->type;if(!scalarType(type)&&type!=Type::Pentarnion&&type!=Type::Element&&type!=Type::ElementMatrix&&type!=Type::Tensor&&type!=Type::Complex&&!matrixType(type)&&!rangeType(type)&&type!=Type::Tuple&&type!=Type::Invalid)reject("Callback point must be numeric coordinates, one algebra element/fixed array or a typed numeric tuple.");}
        if(count>required&&value->children.back()->type!=Type::Record&&value->children.back()->type!=Type::Invalid)reject("Derivative options must be a typed record.");
        if(product&&count>2){const auto type=value->children[2]->type;if(!scalarType(type)&&!matrixType(type)&&type!=Type::ElementMatrix&&type!=Type::Tensor&&!rangeType(type)&&type!=Type::Invalid)reject("Derivative product direction must be real coordinates or a matching fixed-array direction.");}
        const auto function=count?(value->children[0]->function?value->children[0]->function:value->children[0]->schema?value->children[0]->schema->function:nullptr):nullptr;
        if(function){
            if(count>1&&!checkNativeSolverPoint(value->children[1],*function,"E2101"))valid=false;
            for(std::size_t n=0;n<function->parameters.size();++n){const auto& parameter=function->parameters[n];const auto schema=n<function->parameterSchemas.size()?function->parameterSchemas[n]:parameter.schema;if(!nativeSolverScalar(parameter.type,schema))reject("Compiled derivative callback parameters require numeric scalar elements or explicitly shaped native arrays.");}
            const auto outputs=function->result==Type::Tuple?function->results:std::vector<Type>{function->result};
            for(std::size_t n=0;n<outputs.size();++n){const auto type=outputs[n];const auto schema=function->result==Type::Tuple?(function->resultSchema&&n<function->resultSchema->elements.size()?function->resultSchema->elements[n].schema:nullptr):function->resultSchema;if(!nativeSolverScalar(type,schema))reject("Compiled derivative callbacks return numeric scalar elements or explicitly shaped arrays, including typed tuples.");}
            if((name=="gradient"||name=="hessian"||name=="hvp")&&(outputs.size()!=1||outputs[0]!=Type::Real))reject("Gradient, Hessian and HVP require one real scalar objective.");
        }
        if(!valid)return Type::Invalid;
        if(name=="derivative_check"){
            auto schema=std::make_shared<ValueSchema>();
            for(const auto field:{"max_abs_error","relative_error","tolerance"})schema->fields.push_back({field,Type::Real,{}});
            for(const auto field:{"passed","order","native_evaluations","cache_hit","threads_requested","threads_used"})schema->fields.push_back({field,Type::Integer,{}});
            schema->fields.push_back({"cache_key",Type::String,{}});value->schema=std::move(schema);return Type::Record;
        }
        return Type::RealMatrix;
    }
