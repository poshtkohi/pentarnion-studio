    UniversalDefinition universalDefinitionValue(const Value& value,SourceSpan span) const {
        if(const auto* definition=std::get_if<UniversalDefinition>(&value);definition&&*definition)return *definition;
        if(const auto* element=std::get_if<UniversalElement>(&value))return element->definition();
        fail("R2530","Expected an immutable algebra definition or algebra element.",span);
    }
    const UniversalElement& universalElementValue(const Value& value,SourceSpan span) const {
        const auto* element=std::get_if<UniversalElement>(&value);if(!element)fail("R2530","Expected an algebra element.",span);return *element;
    }
    void universalCharge(const UniversalDefinition& definition,SourceSpan span,std::size_t additionalWork=0) {
        const auto dimension=definition->dimension();const auto terms=definition->terms().size();
        const auto base=std::max<std::size_t>(1,dimension+terms);
        if(additionalWork>options_.maxNativeOperations||base>options_.maxNativeOperations-additionalWork)fail("R1005","Algebra operation exceeds the native work allowance.",span);
        chargeNumericWork(base+additionalWork,span);
    }
    Value universalMatrix(const std::vector<double>& values,std::size_t rows,std::size_t columns,SourceSpan span) {
        return numericResult(values,rows,columns,1,span);
    }
    Value universalProperty(const algebra::universal::Property& property,SourceSpan span) {
        return makeRecord({{"value",I(property.value)},{"evidence",std::string(algebra::universal::evidenceName(property.evidence))},{"detail",property.detail}},span);
    }
    Value universalInfo(const UniversalDefinition& definition,SourceSpan span) {
        const auto& properties=definition->properties();std::string basis;
        for(const auto& name:definition->basis()){if(!basis.empty())basis+=",";basis+=name;}
        const auto n=definition->dimension();auto matrix=[&](const std::vector<double>& values){return universalMatrix(values,values.empty()?0:n,values.empty()?0:n,span);};
        return makeRecord({{"format",std::string("StudioAlgebraInfo-v1")},{"name",definition->name()},{"id",definition->id()},{"hash",definition->hash()},
            {"source_hash",definition->sourceHash()},{"builtin",definition->builtinKind()},{"convention",definition->convention()},
            {"coefficient_domain",definition->coefficientDomain()},{"basis_labels",std::move(basis)},{"dimension",I(n)},{"terms",I(definition->terms().size())},
            {"has_unit",I(!definition->unit().empty())},{"has_conjugation",I(!definition->conjugation().empty())},{"has_metric",I(!definition->metric().empty())},
            {"unit",universalMatrix(definition->unit(),definition->unit().empty()?0:1,definition->unit().size(),span)},
            {"conjugation",matrix(definition->conjugation())},{"metric",matrix(definition->metric())},
            {"properties",makeRecord({{"unital",universalProperty(properties.unital,span)},{"commutative",universalProperty(properties.commutative,span)},
                {"associative",universalProperty(properties.associative,span)},{"alternative",universalProperty(properties.alternative,span)},
                {"power_associative",universalProperty(properties.powerAssociative,span)},{"conjugation",universalProperty(properties.conjugation,span)},
                {"positive_metric",universalProperty(properties.positiveMetric,span)},{"division",universalProperty(properties.division,span)}},span)}},span);
    }
    algebra::universal::SolveOptions universalSolveOptions(const Value* value,SourceSpan span) const {
        algebra::universal::SolveOptions options;if(!value)return options;const auto* fields=recordFields(*value);
        if(!fields)fail("R2530","Algebra solve options require a typed record.",span);
        for(const auto& [name,setting]:*fields){const double x=finite(real(setting,span),span);
            if(name=="rank_tolerance")options.rankRelativeTolerance=x;
            else if(name=="condition_limit")options.conditionLimit=x;
            else if(name=="residual_tolerance")options.residualTolerance=x;
            else fail("R2530","Unknown algebra solve option: "+name,span);
        }
        return options;
    }
    Value universalSolveReport(const algebra::universal::SolveResult& solved,SourceSpan span) {
        const bool conditionAvailable=std::isfinite(solved.condition),residualAvailable=std::isfinite(solved.relativeResidual);
        const auto coefficients=solved.solution?solved.solution->coefficients():std::vector<double>{};
        return makeRecord({{"solution",universalMatrix(coefficients,coefficients.size(),1,span)},
            {"has_solution",I(solved.solution.has_value())},{"status",std::string(algebra::universal::solveStatusName(solved.status))},
            {"rank",I(solved.rank)},{"condition",conditionAvailable?solved.condition:0.0},{"condition_available",I(conditionAvailable)},
            {"relative_residual",residualAvailable?solved.relativeResidual:0.0},{"residual_available",I(residualAvailable)}},span);
    }
    std::optional<Value> universalCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        static const std::set<std::string> names{"builtin_algebra","import_algebra","element","algebra_info","algebra_basis","algebra_coefficients","algebra_leftop","algebra_rightop","algebra_solveleft","algebra_solveright","algebra_norm","algebra_json","algebra_products"};
        static const std::set<std::string> overloads{"coeff","conj","norm2","inv","commutator","associator","scalar","pure","solveleft","solveright","leftop","rightop","sin","cos","exp","sinh","cosh"};
        const bool overloaded=overloads.contains(name)&&!arguments.empty()&&std::holds_alternative<UniversalElement>(arguments.front());
        if(!names.contains(name)&&!overloaded)return {};
        if(options_.cancellation&&options_.cancellation->load())fail("R1006","Algebra operation cancelled.",span);
        try{
            if(name=="builtin_algebra"||name=="import_algebra"){
                if(!expression.schema||!expression.schema->algebra)fail("R2530","Algebra definition has no checked immutable source snapshot.",span);
                // Static analysis captured and validated literal data. Never reread a
                // mutable file here or execute source from imported table cells.
                return Value(expression.schema->algebra);
            }
            if(arguments.empty())fail("R2530","Missing algebra argument.",span);
            const auto definition=universalDefinitionValue(arguments.front(),span);const auto n=definition->dimension();
            if(name=="algebra_info"){if(arguments.size()!=1)fail("R2530","Use algebra_info(definition_or_element).",span);universalCharge(definition,span);return universalInfo(definition,span);}
            if(name=="algebra_json"){if(arguments.size()!=1)fail("R2530","Use algebra_json(definition_or_element).",span);universalCharge(definition,span);return Value(definition->json());}
            if(name=="algebra_products"){
                if(arguments.size()!=1)fail("R2530","Use algebra_products(definition_or_element).",span);
                if(definition->terms().size()>options_.maxMatrixElements/4)fail("R1005","Algebra product table exceeds the output-matrix allowance.",span);
                universalCharge(definition,span);std::vector<double> data;data.reserve(definition->terms().size()*4);
                for(const auto& term:definition->terms()){data.push_back(static_cast<double>(term.left+1));data.push_back(static_cast<double>(term.right+1));data.push_back(static_cast<double>(term.output+1));data.push_back(term.coefficient);}
                return universalMatrix(data,definition->terms().size(),4,span);
            }
            if(name=="element"){
                if(arguments.size()<2||!std::holds_alternative<UniversalDefinition>(arguments.front()))fail("R2530","Use element(definition,real_coefficients) or element(definition,a0,...).",span);
                std::vector<double> coefficients;
                if(arguments.size()==2)coefficients=numericVector(arguments[1],span,false);
                else{coefficients.reserve(arguments.size()-1);for(std::size_t i=1;i<arguments.size();++i)coefficients.push_back(finite(real(arguments[i],span),span));}
                if(coefficients.size()!=n)fail("R2530","Element coefficient count differs from its algebra dimension.",span);
                chargeNumericWork(std::max<std::size_t>(1,n),span);return Value(UniversalElement(definition,std::move(coefficients)));
            }
            if(name=="algebra_basis"){
                if(arguments.size()!=2||!std::holds_alternative<UniversalDefinition>(arguments.front()))fail("R2530","Use algebra_basis(definition,index0).",span);
                const auto index=integer(arguments[1],span);if(index<0||static_cast<std::size_t>(index)>=n)fail("R2530","Algebra basis index lies outside0..dimension-1.",span);
                chargeNumericWork(std::max<std::size_t>(1,n),span);return Value(UniversalElement::basis(definition,static_cast<std::size_t>(index)));
            }
            const auto& first=universalElementValue(arguments.front(),span);
            if(name=="algebra_coefficients"){if(arguments.size()!=1)fail("R2530","Use algebra_coefficients(element).",span);return universalMatrix(first.coefficients(),1,n,span);}
            if(name=="coeff"){
                if(arguments.size()!=2)fail("R2530","Use coeff(element,index0).",span);
                const auto index=integer(arguments[1],span);
                if(index<0||static_cast<std::size_t>(index)>=n)fail("R2530","Algebra coefficient index lies outside0..dimension-1.",span);
                return finite(first.coefficients()[static_cast<std::size_t>(index)],span);
            }
            if(name=="scalar"||name=="pure"){
                if(arguments.size()!=1)fail("R2530","scalar/pure accept one element.",span);
                const auto& unit=definition->unit();if(unit.size()!=n||unit.empty()||unit.front()!=1||!std::all_of(unit.begin()+1,unit.end(),[](double x){return x==0;}))
                    fail("R2530","scalar/pure require the unit to be exactly the first basis vector.",span);
                if(name=="scalar")return finite(first.coefficients().front(),span);
                auto coefficients=first.coefficients();coefficients.front()=0;return Value(UniversalElement(definition,std::move(coefficients)));
            }
            const bool factorization=name=="algebra_solveleft"||name=="algebra_solveright"||name=="solveleft"||name=="solveright"||name=="inv";
            const bool elementary=name=="sin"||name=="cos"||name=="exp"||name=="sinh"||name=="cosh";
            if(elementary&&arguments.size()!=1)fail("R2530","Whole-element elementary functions accept one element.",span);
            const auto& kind=definition->builtinKind();
            const bool quadratic=kind=="real"||kind=="complex"||kind=="quaternion"||kind=="octonion"||kind=="sedenion"||kind=="dual"||kind=="split_complex"||kind=="split_quaternion";
            const auto seriesWork=elementary&&!quadratic?159*(n+definition->terms().size()):0;
            universalCharge(definition,span,factorization?n*n*n:seriesWork);
            Value result=I(0);
            ThreadBudget::instance().withReservation([&]{
                if(name=="algebra_norm"){result=finite(algebra::universal::coordinateNorm(first),span);return;}
                if(name=="norm2"){result=finite(algebra::universal::metricNormSquared(first),span);return;}
                if(name=="conj"){result=algebra::universal::conjugate(first);return;}
                if(name=="inv"){result=algebra::universal::inverse(first);return;}
                if(name=="sin"){result=algebra::universal::sine(first);return;}
                if(name=="cos"){result=algebra::universal::cosine(first);return;}
                if(name=="exp"){result=algebra::universal::exponential(first);return;}
                if(name=="sinh"){result=algebra::universal::hyperbolicSine(first);return;}
                if(name=="cosh"){result=algebra::universal::hyperbolicCosine(first);return;}
                if(name=="algebra_leftop"||name=="leftop"||name=="algebra_rightop"||name=="rightop"){
                    const auto matrix=name=="algebra_leftop"||name=="leftop"?algebra::universal::leftMatrix(first):algebra::universal::rightMatrix(first);
                    result=universalMatrix(matrix,n,n,span);return;
                }
                if(name=="commutator"){
                    if(arguments.size()!=2)fail("R2530","Use commutator(a,b).",span);
                    result=algebra::universal::commutator(first,universalElementValue(arguments[1],span));return;
                }
                if(name=="associator"){
                    if(arguments.size()!=3)fail("R2530","Use associator(a,b,c).",span);
                    result=algebra::universal::associator(first,universalElementValue(arguments[1],span),universalElementValue(arguments[2],span));return;
                }
                if(name=="algebra_solveleft"||name=="algebra_solveright"||name=="solveleft"||name=="solveright"){
                    const bool report=name=="algebra_solveleft"||name=="algebra_solveright";
                    if(arguments.size()!=2&&!(report&&arguments.size()==3))fail("R2530","Use algebra_solveleft/right(a,b[,options]) or solveleft/right(a,b).",span);
                    const auto options=universalSolveOptions(arguments.size()==3?&arguments[2]:nullptr,span);
                    const auto& second=universalElementValue(arguments[1],span);
                    const auto solved=name=="algebra_solveleft"||name=="solveleft"?algebra::universal::solveLeft(first,second,options):algebra::universal::solveRight(first,second,options);
                    if(report)result=universalSolveReport(solved,span);
                    else{if(!solved.solution)fail("R2531","Algebra equation has no accepted unique solution: "+std::string(algebra::universal::solveStatusName(solved.status))+". Use algebra_solveleft/right to inspect its rank/residual report.",span);result=*solved.solution;}
                    return;
                }
                fail("R2530","Unsupported algebra operation.",span);
            });
            ++parallel_.statistics.serialCalls;parallel_.statistics.threadsUsed=1;return result;
        }catch(const algebra::universal::Error& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":error.code,error.what(),span);}
    }
